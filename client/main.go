package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"math/rand"
	"net/http"
	"os/exec"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"time"
)

type NetworkInfo struct {
	Name string `json:"name"`
	MAC  string `json:"mac"`
	IPv4 string `json:"ipv4"`
	IPv6 string `json:"ipv6"`
}

type ClientInfo struct {
	ClientID    string        `json:"client_id"`
	Hostname    string        `json:"hostname"`
	Arch        string        `json:"arch"`
	Kernel      string        `json:"kernel"`
	Uptime      int64         `json:"uptime"`
	Load1       float64       `json:"load1"`
	Load5       float64       `json:"load5"`
	Load15      float64       `json:"load15"`
	MemoryTotal uint64        `json:"memory_total"`
	MemoryUsed  uint64        `json:"memory_used"`
	Timestamp   time.Time     `json:"timestamp"`
	Network     []NetworkInfo `json:"network"`
}

func generateClientID() string {
	const chars = "abcdefghijklmnopqrstuvwxyz0123456789"
	id := make([]byte, 6)
	for i := range id {
		id[i] = chars[rand.Intn(len(chars))]
	}
	return string(id)
}

func runCommand(name string, arg ...string) string {
	cmd := exec.Command(name, arg...)
	out, err := cmd.Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

func isSkipInterface(name string) bool {
	skipPrefixes := []string{"lo", "docker", "veth", "br-", "utun", "bridge", "flannel", "cni"}
	lowerName := strings.ToLower(name)
	for _, prefix := range skipPrefixes {
		if strings.HasPrefix(lowerName, prefix) {
			return true
		}
	}
	return false
}

func parseUname() (hostname, arch, kernel string) {
	output := runCommand("uname", "-a")
	if output == "" {
		return
	}

	fields := strings.Fields(output)
	if len(fields) >= 5 {
		hostname = fields[1]
		kernel = fields[2]
		arch = fields[len(fields)-2]
	}
	return
}

func parseUptime() int64 {
	output := runCommand("uptime", "-s")
	if output != "" {
		t, err := time.Parse("2006-01-02 15:04:05", output)
		if err == nil {
			return int64(time.Since(t).Seconds())
		}
	}

	output = runCommand("cat", "/proc/uptime")
	if output != "" {
		fields := strings.Fields(output)
		if len(fields) > 0 {
			uptimeSec, _ := strconv.ParseInt(strings.Split(fields[0], ".")[0], 10, 64)
			return uptimeSec
		}
	}
	return 0
}

func parseLoad() (float64, float64, float64) {
	output := runCommand("cat", "/proc/loadavg")
	if output == "" {
		return 0, 0, 0
	}

	fields := strings.Fields(output)
	if len(fields) >= 3 {
		load1, _ := strconv.ParseFloat(fields[0], 64)
		load5, _ := strconv.ParseFloat(fields[1], 64)
		load15, _ := strconv.ParseFloat(fields[2], 64)
		return load1, load5, load15
	}
	return 0, 0, 0
}

func parseMemory() (uint64, uint64) {
	output := runCommand("cat", "/proc/meminfo")
	if output == "" {
		return 0, 0
	}

	var total, available uint64
	re := regexp.MustCompile(`(\w+):\s+(\d+)\s+kB`)
	for _, match := range re.FindAllStringSubmatch(output, -1) {
		if len(match) >= 3 {
			key := match[1]
			val, _ := strconv.ParseUint(match[2], 10, 64)
			val *= 1024
			if key == "MemTotal" {
				total = val
			} else if key == "MemAvailable" {
				available = val
			}
		}
	}
	used := total - available
	return total, used
}

func parseNetworkLinux() []NetworkInfo {
	output := runCommand("ip", "a")
	if output == "" {
		return nil
	}

	var networks []NetworkInfo
	var currentNet NetworkInfo
	inetFound := false
	inet6Found := false
	macFound := false

	lines := strings.Split(output, "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)

		if strings.HasPrefix(line, " ") && !strings.HasPrefix(line, "\t") {
			continue
		}

		if match, _ := regexp.MatchString(`^\d+:\s+\w+`, line); match {
			if currentNet.Name != "" && !isSkipInterface(currentNet.Name) {
				networks = append(networks, currentNet)
			}
			fields := strings.Fields(line)
			if len(fields) >= 2 {
				currentNet = NetworkInfo{
					Name: strings.TrimSuffix(fields[1], ":"),
				}
			}
			inetFound = false
			inet6Found = false
			macFound = false
			continue
		}

		if strings.Contains(line, "inet ") && !inetFound && !isSkipInterface(currentNet.Name) {
			re := regexp.MustCompile(`inet\s+(\d+\.\d+\.\d+\.\d+)`)
			if match := re.FindStringSubmatch(line); len(match) > 1 {
				currentNet.IPv4 = match[1]
				inetFound = true
			}
		}

		if strings.Contains(line, "inet6 ") && !inet6Found && !isSkipInterface(currentNet.Name) {
			re := regexp.MustCompile(`inet6\s+([a-fA-F0-9:]+)`)
			if match := re.FindStringSubmatch(line); len(match) > 1 {
				currentNet.IPv6 = match[1]
				inet6Found = true
			}
		}

		if strings.Contains(line, "link") && !macFound && !isSkipInterface(currentNet.Name) {
			re := regexp.MustCompile(`link/\w+\s+([a-fA-F0-9:]+)`)
			if match := re.FindStringSubmatch(line); len(match) > 1 {
				currentNet.MAC = match[1]
				macFound = true
			}
		}
	}

	if currentNet.Name != "" && !isSkipInterface(currentNet.Name) {
		networks = append(networks, currentNet)
	}

	return networks
}

func parseNetworkWindows() []NetworkInfo {
	output := runCommand("ipconfig")
	if output == "" {
		return nil
	}

	var networks []NetworkInfo
	var currentNet NetworkInfo

	lines := strings.Split(output, "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)

		if strings.HasSuffix(line, ":") {
			adapterName := strings.TrimSuffix(line, ":")
			if currentNet.Name != "" {
				networks = append(networks, currentNet)
			}
			currentNet = NetworkInfo{Name: adapterName}
			continue
		}

		if strings.Contains(line, "IPv4") {
			re := regexp.MustCompile(`IPv4.*?:\s*(\d+\.\d+\.\d+\.\d+)`)
			if match := re.FindStringSubmatch(line); len(match) > 1 {
				currentNet.IPv4 = match[1]
			}
		}

		if strings.Contains(line, "IPv6") {
			re := regexp.MustCompile(`IPv6.*?:\s*([a-fA-F0-9:]+)`)
			if match := re.FindStringSubmatch(line); len(match) > 1 {
				currentNet.IPv6 = match[1]
			}
		}

		if strings.Contains(line, "Physical Address") {
			re := regexp.MustCompile(`([a-fA-F0-9-]{17})`)
			if match := re.FindStringSubmatch(line); len(match) > 1 {
				currentNet.MAC = strings.ReplaceAll(match[1], "-", ":")
			}
		}
	}

	if currentNet.Name != "" {
		networks = append(networks, currentNet)
	}

	return networks
}

func collectInfo(clientID string) ClientInfo {
	var info ClientInfo
	info.ClientID = clientID
	info.Timestamp = time.Now()

	if runtime.GOOS == "windows" {
		hostname, arch, _ := parseUname()
		info.Hostname = hostname
		info.Arch = arch
		info.Kernel = runtime.GOOS
	} else {
		info.Hostname, info.Arch, info.Kernel = parseUname()
	}

	info.Uptime = parseUptime()
	info.Load1, info.Load5, info.Load15 = parseLoad()
	info.MemoryTotal, info.MemoryUsed = parseMemory()

	if runtime.GOOS == "windows" {
		info.Network = parseNetworkWindows()
	} else {
		info.Network = parseNetworkLinux()
	}

	return info
}

func sendData(url string, info ClientInfo, quiet bool) error {
	jsonData, err := json.Marshal(info)
	if err != nil {
		return err
	}

	resp, err := http.Post(url, "application/json", bytes.NewBuffer(jsonData))
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("server returned status %d", resp.StatusCode)
	}

	if !quiet {
		body, _ := io.ReadAll(resp.Body)
		fmt.Printf("Sent to %s: %s\n", url, string(body))
	}
	return nil
}

func main() {
	clientID := flag.String("id", "", "Client identifier")
	interval := flag.Int("interval", 60, "Interval in seconds")
	i := flag.Int("i", 60, "Short for interval")
	quiet := flag.Bool("quiet", false, "Suppress console output")

	flag.Parse()

	if *interval == 60 && *i != 60 {
		*interval = *i
	}

	if flag.NArg() == 0 {
		fmt.Println("Usage: client --id <id> --interval <seconds> [--quiet] <url> [url...]")
		flag.PrintDefaults()
		return
	}

	urls := flag.Args()

	if *clientID == "" {
		*clientID = generateClientID()
	}

	if !*quiet {
		fmt.Printf("Client ID: %s\n", *clientID)
		fmt.Printf("Interval: %d seconds\n", *interval)
		fmt.Printf("URLs: %v\n", urls)
		fmt.Println("Starting monitoring...")
	}

	ticker := time.NewTicker(time.Duration(*interval) * time.Second)
	defer ticker.Stop()

	for {
		info := collectInfo(*clientID)
		if !*quiet {
			fmt.Printf("Collected: hostname=%s, load=%.2f, memory=%d/%d\n",
				info.Hostname, info.Load1, info.MemoryUsed, info.MemoryTotal)
		}

		for _, url := range urls {
			if err := sendData(url, info, *quiet); err != nil {
				if !*quiet {
					fmt.Printf("Failed to send to %s: %v\n", url, err)
				}
			}
		}

		<-ticker.C
	}
}
