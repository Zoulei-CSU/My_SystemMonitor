package main

import (
	_ "embed"
	"encoding/json"
	"flag"
	"fmt"
	"html/template"
	"io"
	"log"
	"net/http"
	"sort"
	"sync"
	"time"
)

//go:embed chart.js
var chartJS string

const (
	historySize    = 128
	offlineTimeout = 2 * time.Hour
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
	Timestamp   string        `json:"timestamp"`
	Network     []NetworkInfo `json:"network"`
}

type ClientHistory struct {
	LastSeen    time.Time
	LoadHistory []float64
	MemHistory  []uint64
	TimeHistory []time.Time
	LatestInfo  ClientInfo
}

type Server struct {
	mu        sync.RWMutex
	clients   map[string]*ClientHistory
	urlPrefix string
	quiet     bool
}

func NewServer(urlPrefix string) *Server {
	return &Server{
		clients:   make(map[string]*ClientHistory),
		urlPrefix: urlPrefix,
	}
}

func (s *Server) handlePut(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	bodyBytes, _ := io.ReadAll(r.Body)
	var info ClientInfo
	if err := json.Unmarshal(bodyBytes, &info); err != nil {
		if !s.quiet {
			log.Printf("Failed to decode JSON: %v, body: %s", err, string(bodyBytes))
		}
		http.Error(w, "Invalid JSON", http.StatusBadRequest)
		return
	}

	s.mu.Lock()
	if !s.quiet {
		log.Printf("Received data from client: %s (%s)", info.Hostname, info.ClientID)
	}
	client, exists := s.clients[info.ClientID]
	if !exists {
		client = &ClientHistory{
			LoadHistory: make([]float64, 0, historySize),
			MemHistory:  make([]uint64, 0, historySize),
			TimeHistory: make([]time.Time, 0, historySize),
		}
		s.clients[info.ClientID] = client
	}

	client.LastSeen = time.Now()
	client.LatestInfo = info

	if len(client.LoadHistory) >= historySize {
		client.LoadHistory = client.LoadHistory[1:]
	}
	client.LoadHistory = append(client.LoadHistory, info.Load1)

	if len(client.MemHistory) >= historySize {
		client.MemHistory = client.MemHistory[1:]
	}
	client.MemHistory = append(client.MemHistory, info.MemoryUsed)

	if len(client.TimeHistory) >= historySize {
		client.TimeHistory = client.TimeHistory[1:]
	}
	client.TimeHistory = append(client.TimeHistory, time.Now())

	s.mu.Unlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "ok"})
}

func (s *Server) isOnline(clientID string) bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	client, exists := s.clients[clientID]
	if !exists {
		return false
	}
	return time.Since(client.LastSeen) < offlineTimeout
}

func (s *Server) getClientInfo() []map[string]interface{} {
	s.mu.RLock()
	defer s.mu.RUnlock()

	result := make([]map[string]interface{}, 0, len(s.clients))
	now := time.Now()

	clientIDs := make([]string, 0, len(s.clients))
	for id := range s.clients {
		clientIDs = append(clientIDs, id)
	}
	sort.Strings(clientIDs)

	for _, id := range clientIDs {
		client := s.clients[id]
		isOnline := now.Sub(client.LastSeen) < offlineTimeout
		info := client.LatestInfo

		var primaryIP string
		if len(info.Network) > 0 {
			primaryIP = info.Network[0].IPv4
		}

		result = append(result, map[string]interface{}{
			"client_id":    id,
			"hostname":     info.Hostname,
			"arch":         info.Arch,
			"kernel":       info.Kernel,
			"uptime":       info.Uptime,
			"load1":        info.Load1,
			"load5":        info.Load5,
			"load15":       info.Load15,
			"memory_total": info.MemoryTotal,
			"memory_used":  info.MemoryUsed,
			"timestamp":    info.Timestamp,
			"network":      info.Network,
			"ip":           primaryIP,
			"is_online":    isOnline,
			"last_seen":    client.LastSeen,
			"load_history": client.LoadHistory,
			"mem_history":  client.MemHistory,
			"time_history": client.TimeHistory,
		})
	}

	return result
}

func (s *Server) handleIndex(w http.ResponseWriter, r *http.Request) {
	tmpl := `<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>System Monitor</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: Arial, sans-serif; background: #f5f5f5; height: 100vh; display: flex; flex-direction: column; }
        header { background: #333; color: white; padding: 15px 20px; display: flex; justify-content: space-between; align-items: center; }
        header h1 { margin: 0; font-size: 20px; }
        .auto-refresh-btn { background: #4CAF50; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; font-size: 14px; }
        .auto-refresh-btn.off { background: #666; }
        .container { display: flex; flex: 1; overflow: hidden; }
        .sidebar { width: 280px; background: white; border-right: 1px solid #ddd; overflow-y: auto; padding: 10px; }
        .client-item { padding: 12px; margin: 5px 0; border-radius: 6px; cursor: pointer; border: 2px solid transparent; transition: all 0.2s; }
        .client-item:hover { background: #f0f0f0; }
        .client-item.active { border-color: #4CAF50; background: #e8f5e9; }
        .client-item.online { border-left: 4px solid #4CAF50; }
        .client-item.offline { border-left: 4px solid #f44336; opacity: 0.7; }
        .client-item h3 { font-size: 14px; margin-bottom: 5px; }
        .client-item .status { font-size: 12px; padding: 2px 6px; border-radius: 3px; }
        .client-item .status.online { background: #4CAF50; color: white; }
        .client-item .status.offline { background: #f44336; color: white; }
        .main { flex: 1; padding: 20px; overflow-y: auto; }
        .detail { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); display: none; }
        .detail.active { display: block; }
        .detail h2 { color: #333; margin-bottom: 15px; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }
        table { border-collapse: collapse; width: 100%; margin: 15px 0; }
        th, td { border: 1px solid #ddd; padding: 10px; text-align: left; }
        th { background: #4CAF50; color: white; }
        .network-table { font-size: 14px; margin-top: 15px; }
        .network-table th { background: #666; }
        .chart-container { margin: 20px 0; }
        .chart-container h3 { color: #333; margin-bottom: 10px; }
        .chart-wrapper { position: relative; height: 200px; width: 100%; }
        .empty-state { text-align: center; padding: 50px; color: #666; }
    </style>
</head>
<body>
    <header>
        <h1>System Monitor</h1>
        <button id="autoRefreshBtn" class="auto-refresh-btn" onclick="toggleAutoRefresh()">Auto Refresh: ON</button>
    </header>
    <div class="container">
        <div class="sidebar">
            {{range .}}
            <div class="client-item {{if .is_online}}online{{else}}offline{{end}}" onclick="showDetail('{{.client_id}}')">
                <h3>{{.client_id}}</h3>
                <span class="status {{if .is_online}}online{{else}}offline{{end}}">{{if .is_online}}Online{{else}}Offline{{end}}</span>
                <div style="font-size:12px;color:#666;">{{.ip}}</div>
            </div>
            {{else}}
            <p style="padding:20px;color:#666;">No clients connected</p>
            {{end}}
        </div>
        <div class="main">
            {{range .}}
            <div id="detail-{{.client_id}}" class="detail">
                <h2>{{.hostname}} ({{.client_id}})</h2>
                <span class="status {{if .is_online}}online{{else}}offline{{end}}">{{if .is_online}}Online{{else}}Offline{{end}}</span>
                <span style="margin-left:10px;color:#666;">Last Seen: {{.last_seen.Format "2006-01-02T15:04:05Z07:00"}} ({{.last_seen | ago}} ago)</span>
                <table>
                    <tr><th>Property</th><th>Value</th></tr>
                    <tr><td>IP Address</td><td>{{.ip}}</td></tr>
                    <tr><td>Architecture</td><td>{{.arch}}</td></tr>
                    <tr><td>Kernel</td><td>{{.kernel}}</td></tr>
                    <tr><td>Uptime</td><td>{{formatUptime .uptime}}</td></tr>
                    <tr><td>Load (1/5/15 min)</td><td>{{.load1}} / {{.load5}} / {{.load15}}</td></tr>
                    <tr><td>Memory</td><td>{{formatBytes .memory_used}} / {{formatBytes .memory_total}} ({{printf "%.1f" (div .memory_used .memory_total)}}%)</td></tr>
                    <tr><td>Timestamp</td><td>{{.timestamp}}</td></tr>
                </table>
                {{if .network}}
                <h3>Network Interfaces</h3>
                <table class="network-table">
                    <tr><th>Name</th><th>MAC</th><th>IPv4</th><th>IPv6</th></tr>
                    {{range .network}}
                    <tr><td>{{.Name}}</td><td>{{.MAC}}</td><td>{{.IPv4}}</td><td>{{.IPv6}}</td></tr>
                    {{end}}
                </table>
                {{end}}
                <div class="chart-container">
                    <h3>Load History</h3>
                    <div class="chart-wrapper"><canvas id="load-chart-{{.client_id}}"></canvas></div>
                </div>
                <div class="chart-container">
                    <h3>Memory History</h3>
                    <div class="chart-wrapper"><canvas id="mem-chart-{{.client_id}}"></canvas></div>
                </div>
            </div>
            {{end}}
            <div id="empty-state" class="empty-state" {{if .}}style="display:none"{{end}}>
                <h2>Select a client to view details</h2>
            </div>
        </div>
    </div>
    <script>
        var chartScript = document.createElement('script');
        chartScript.textContent = {{chartJS}};
        document.head.appendChild(chartScript);
    </script>
    <script>
        var clients = {{.}};
        var loadCharts = {};
        var autoRefreshEnabled = true;
        var refreshIntervalId = null;
        
        function toggleAutoRefresh() {
            autoRefreshEnabled = !autoRefreshEnabled;
            var btn = document.getElementById('autoRefreshBtn');
            if (autoRefreshEnabled) {
                btn.textContent = 'Auto Refresh: ON';
                btn.classList.remove('off');
                startRefreshInterval();
            } else {
                btn.textContent = 'Auto Refresh: OFF';
                btn.classList.add('off');
                stopRefreshInterval();
            }
        }
        
        function startRefreshInterval() {
            if (refreshIntervalId) clearInterval(refreshIntervalId);
            refreshIntervalId = setInterval(doRefresh, 1000);
        }
        
        function stopRefreshInterval() {
            if (refreshIntervalId) {
                clearInterval(refreshIntervalId);
                refreshIntervalId = null;
            }
        }
        
        function doRefresh() {
            var urlBase = window.location.pathname.replace('/index.html', '');
            fetch(urlBase + '/data')
                .then(function(response) {
                    if (!response.ok) throw new Error('Network response was not ok');
                    return response.json();
                })
                .then(function(newClients) {
                    clients = newClients;
                    
                    var oldSidebar = document.querySelector('.sidebar');
                    var newSidebarHTML = '';
                    for (var i = 0; i < newClients.length; i++) {
                        var c = newClients[i];
                        var statusClass = c.is_online ? 'online' : 'offline';
                        var statusText = c.is_online ? 'Online' : 'Offline';
                        newSidebarHTML += '<div class="client-item ' + statusClass + '" onclick="showDetail(\'' + c.client_id + '\')"><h3>' + c.client_id + '</h3><span class="status ' + statusClass + '">' + statusText + '</span><div style="font-size:12px;color:#666;">' + c.ip + '</div></div>';
                    }
                    oldSidebar.innerHTML = newSidebarHTML || '<p style="padding:20px;color:#666;">No clients connected</p>';
                    
                    var activeDetail = document.querySelector('.detail.active');
                    if (activeDetail) {
                        var clientId = activeDetail.id.replace('detail-', '');
                        var client = clients.find(function(c) { return c.client_id === clientId; });
                        if (!client) {
                            if (clients.length > 0) {
                                showDetail(clients[0].client_id);
                            }
                        } else {
                            var h2 = activeDetail.querySelector('h2');
                            var statusSpan = activeDetail.querySelector('.detail .status');
                            var lastSeenSpan = activeDetail.querySelector('.detail span:last-of-type');
                            
                            if (h2) h2.textContent = client.hostname + ' (' + client.client_id + ')';
                            if (statusSpan) {
                                statusSpan.className = 'status ' + (client.is_online ? 'online' : 'offline');
                                statusSpan.textContent = client.is_online ? 'Online' : 'Offline';
                            }
                            var agoText = '';
                            var diffSecs = Math.floor((new Date() - new Date(client.last_seen)) / 1000);
                            if (diffSecs >= 86400) agoText = Math.floor(diffSecs / 86400) + 'd';
                            else if (diffSecs >= 3600) agoText = Math.floor(diffSecs / 3600) + 'h';
                            else if (diffSecs >= 60) agoText = Math.floor(diffSecs / 60) + 'm';
                            else agoText = diffSecs + 's';
                            if (lastSeenSpan) lastSeenSpan.textContent = 'Last Seen: ' + client.last_seen + ' (' + agoText + ' ago)';
                            
                            var rows = activeDetail.querySelectorAll('table:first-of-type tr');
                            rows.forEach(function(row) {
                                var td = row.querySelector('td');
                                if (!td) return;
                                var label = td.textContent;
                                if (label === 'IP Address') {
                                    row.querySelector('td:last-child').textContent = client.ip || '';
                                } else if (label === 'Architecture') {
                                    row.querySelector('td:last-child').textContent = client.arch || '';
                                } else if (label === 'Kernel') {
                                    row.querySelector('td:last-child').textContent = client.kernel || '';
                                } else if (label === 'Uptime') {
                                    var secs = client.uptime;
                                    var days = Math.floor(secs / 86400);
                                    var hours = Math.floor((secs % 86400) / 3600);
                                    var mins = Math.floor((secs % 3600) / 60);
                                    var uptimeStr = '';
                                    if (days > 0) uptimeStr = days + 'd ' + hours + 'h ' + mins + 'm';
                                    else if (hours > 0) uptimeStr = hours + 'h ' + mins + 'm';
                                    else uptimeStr = mins + 'm';
                                    row.querySelector('td:last-child').textContent = uptimeStr;
                                } else if (label === 'Load (1/5/15 min)') {
                                    row.querySelector('td:last-child').textContent = client.load1.toFixed(2) + ' / ' + client.load5.toFixed(2) + ' / ' + client.load15.toFixed(2);
                                } else if (label === 'Memory') {
                                    var memUsed = (client.memory_used / (1024*1024)).toFixed(1);
                                    var memTotal = (client.memory_total / (1024*1024)).toFixed(1);
                                    var memPct = client.memory_total > 0 ? (client.memory_used / client.memory_total * 100).toFixed(1) : 0;
                                    row.querySelector('td:last-child').textContent = memUsed + ' MB / ' + memTotal + ' MB (' + memPct + '%)';
                                } else if (label === 'Timestamp') {
                                    row.querySelector('td:last-child').textContent = client.timestamp;
                                }
                            });
                            
                            var chartKey1 = clientId + '-load';
                            var chartKey2 = clientId + '-mem';
                            if (loadCharts[chartKey1]) loadCharts[chartKey1].destroy();
                            if (loadCharts[chartKey2]) loadCharts[chartKey2].destroy();
                            
                            var canvases = activeDetail.querySelectorAll('canvas');
                            for (var i = 0; i < canvases.length; i++) {
                                canvases[i].remove();
                            }
                            
                            var chartWrappers = activeDetail.querySelectorAll('.chart-wrapper');
                            for (var i = 0; i < chartWrappers.length; i++) {
                                var newCanvas = document.createElement('canvas');
                                newCanvas.id = (i === 0 ? 'load-chart-' : 'mem-chart-') + clientId;
                                chartWrappers[i].appendChild(newCanvas);
                            }
                            
                            loadCharts = {};
                            createOrUpdateChart(clientId, 'load', client);
                            createOrUpdateChart(clientId, 'mem', client);
                        }
                    }
                })
                .catch(function(err) { console.log('Refresh error:', err); });
        }
        
        function showDetail(clientId) {
            document.querySelectorAll('.detail').forEach(function(el) { el.classList.remove('active'); });
            document.querySelectorAll('.client-item').forEach(function(el) { el.classList.remove('active'); });
            
            var detail = document.getElementById('detail-' + clientId);
            if (detail) {
                detail.classList.add('active');
                document.getElementById('empty-state').style.display = 'none';
            }
            
            var clientItem = document.querySelector('.client-item[onclick*="' + clientId + '"]');
            if (clientItem) {
                clientItem.classList.add('active');
            }
            
            var client = clients.find(function(c) { return c.client_id === clientId; });
            if (client) {
                createOrUpdateChart(clientId, 'load', client);
                createOrUpdateChart(clientId, 'mem', client);
            }
        }
        
        function formatTime(date) {
            return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
        }
        
        function formatMemMB(bytes) {
            return (bytes / (1024 * 1024)).toFixed(0);
        }
        
        function createOrUpdateChart(clientId, type, client) {
            var canvasId = type + '-chart-' + clientId;
            var canvas = document.getElementById(canvasId);
            if (!canvas) return;
            
            var data, label, color, maxY;
            if (type === 'load') {
                data = client.load_history;
                label = 'System Load';
                color = '#4CAF50';
                maxY = null;
            } else {
                data = client.mem_history;
                label = 'Memory (MB)';
                color = '#2196F3';
                maxY = client.memory_total / (1024 * 1024);
            }
            
            var timeData = client.time_history || [];
            var labels = timeData.map(function(d) { return formatTime(new Date(d)); });
            var chartData = type === 'mem' ? data.map(formatMemMB) : data;
            
            var chartKey = clientId + '-' + type;
            if (loadCharts[chartKey]) {
                loadCharts[chartKey].data.labels = labels;
                loadCharts[chartKey].data.datasets[0].data = chartData;
                loadCharts[chartKey].update();
            } else {
                var ctx = canvas.getContext('2d');
                loadCharts[chartKey] = new Chart(ctx, {
                    type: 'line',
                    data: {
                        labels: labels,
                        datasets: [{
                            label: label,
                            data: chartData,
                            borderColor: color,
                            backgroundColor: color + '33',
                            tension: 0.1,
                            fill: true,
                            pointRadius: 0,
                            borderWidth: 2
                        }]
                    },
                    options: {
                        responsive: true,
                        maintainAspectRatio: false,
                        animation: false,
                        scales: {
                            x: {
                                display: true,
                                title: { display: true, text: 'Server Time' },
                                ticks: { maxTicksLimit: 10, autoSkip: true }
                            },
                            y: {
                                display: true,
                                beginAtZero: type === 'mem',
                                suggestedMax: maxY
                            }
                        },
                        plugins: {
                            legend: { display: false }
                        }
                    }
                });
            }
        }
        
        if (clients.length > 0) {
            showDetail(clients[0].client_id);
        }
        
        startRefreshInterval();
    </script>
</body>
</html>`

	funcMap := template.FuncMap{
		"chartJS": func() string { return chartJS },
		"div": func(a, b uint64) float64 {
			if b == 0 {
				return 0
			}
			return float64(a) / float64(b) * 100
		},
		"ago": func(t time.Time) string {
			d := time.Since(t)
			if d >= 24*time.Hour {
				return fmt.Sprintf("%dd", d/24*time.Hour)
			} else if d >= time.Hour {
				return fmt.Sprintf("%dh", d/time.Hour)
			} else if d >= time.Minute {
				return fmt.Sprintf("%dm", d/time.Minute)
			} else {
				return fmt.Sprintf("%ds", int(d.Seconds()))
			}
		},
		"formatUptime": func(seconds int64) string {
			days := seconds / 86400
			hours := (seconds % 86400) / 3600
			mins := (seconds % 3600) / 60
			if days > 0 {
				return fmt.Sprintf("%dd %dh %dm", days, hours, mins)
			} else if hours > 0 {
				return fmt.Sprintf("%dh %dm", hours, mins)
			} else {
				return fmt.Sprintf("%dm", mins)
			}
		},
		"formatBytes": func(bytes uint64) string {
			const unit = 1024
			if bytes < unit {
				return fmt.Sprintf("%d B", bytes)
			}
			div, exp := uint64(unit), 0
			for n := bytes / unit; n >= unit; n /= unit {
				div *= unit
				exp++
			}
			return fmt.Sprintf("%.1f %cB", float64(bytes)/float64(div), "KMGTPE"[exp])
		},
	}

	t, err := template.New("index").Funcs(funcMap).Parse(tmpl)
	if err != nil {
		log.Printf("Template error: %v", err)
		http.Error(w, "Internal error", http.StatusInternalServerError)
		return
	}

	clients := s.getClientInfo()
	if err := t.Execute(w, clients); err != nil {
		log.Printf("Template execute error: %v", err)
	}
}

func (s *Server) handleData(w http.ResponseWriter, r *http.Request) {
	clients := s.getClientInfo()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(clients)
}

func main() {
	port := flag.Int("port", 0, "Port to listen on")
	urlPrefix := flag.String("url", "", "URL prefix path")
	quiet := flag.Bool("quiet", false, "Suppress console output")
	flag.Parse()

	if *port == 0 || *urlPrefix == "" {
		fmt.Println("Usage: server --port <port> --url <url-prefix> [--quiet]")
		flag.PrintDefaults()
		return
	}

	server := NewServer(*urlPrefix)
	server.quiet = *quiet

	mux := http.NewServeMux()
	redirectPath := "/" + *urlPrefix + "/index.html"
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/" {
			http.Redirect(w, r, redirectPath, http.StatusMovedPermanently)
			return
		}
	})
	mux.HandleFunc("/"+*urlPrefix+"/put", server.handlePut)
	mux.HandleFunc("/"+*urlPrefix+"/index.html", server.handleIndex)
	mux.HandleFunc("/"+*urlPrefix+"/data", server.handleData)

	addr := fmt.Sprintf(":%d", *port)
	if !*quiet {
		fmt.Printf("Server starting on http://localhost%s\n", addr)
		fmt.Printf("PUT endpoint: http://localhost%s/%s/put\n", addr, *urlPrefix)
		fmt.Printf("Web UI: http://localhost%s/%s/index.html\n", addr, *urlPrefix)
	}

	if err := http.ListenAndServe(addr, mux); err != nil {
		log.Fatal(err)
	}
}
