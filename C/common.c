#include "common.h"

int starts_with(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

void trim(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

void get_uptime(long *uptime_sec) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (fp) {
        double uptime;
        if (fscanf(fp, "%lf", &uptime) == 1) {
            *uptime_sec = (long)uptime;
        }
        fclose(fp);
    }
}

void get_load(double *load1, double *load5, double *load15) {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (fp) {
        if (fscanf(fp, "%lf %lf %lf", load1, load5, load15) != 3) {
            *load1 = *load5 = *load15 = 0;
        }
        fclose(fp);
    }
}

void get_memory(unsigned long *total, unsigned long *used) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    
    unsigned long mem_total = 0, mem_free = 0, mem_available = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), fp)) {
        if (starts_with(line, "MemTotal:")) {
            sscanf(line, "MemTotal: %lu kB", &mem_total);
        } else if (starts_with(line, "MemAvailable:")) {
            sscanf(line, "MemAvailable: %lu kB", &mem_available);
        } else if (starts_with(line, "MemFree:")) {
            sscanf(line, "MemFree: %lu kB", &mem_free);
        }
    }
    fclose(fp);
    
    *total = mem_total * 1024;
    if (mem_available == 0) {
        *used = (mem_total - mem_free) * 1024;
    } else {
        *used = (mem_total - mem_available) * 1024;
    }
}

void get_network_info(NetworkInfo *networks, int *count) {
    const char *skip_prefixes[] = {"lo", "docker", "veth", "br-", "utun", "bridge", "flannel", "cni", "wl"};
    int skip_count = 9;
    
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;
    
    *count = 0;
    
    for (ifa = ifaddr; ifa != NULL && *count < MAX_NETWORKS; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        int skip = 0;
        for (int i = 0; i < skip_count; i++) {
            if (starts_with(ifa->ifa_name, skip_prefixes[i])) {
                skip = 1;
                break;
            }
        }
        if (skip) continue;
        
        if (ifa->ifa_addr->sa_family == AF_PACKET) {
            if (ifa->ifa_data != NULL) {
                struct sockaddr_ll *link_addr = (struct sockaddr_ll *)ifa->ifa_addr;
                if (link_addr->sll_halen == 6) {
                    char mac_str[18];
                    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                        (unsigned char)link_addr->sll_addr[0],
                        (unsigned char)link_addr->sll_addr[1],
                        (unsigned char)link_addr->sll_addr[2],
                        (unsigned char)link_addr->sll_addr[3],
                        (unsigned char)link_addr->sll_addr[4],
                        (unsigned char)link_addr->sll_addr[5]);
                    
                    int idx = -1;
                    for (int i = 0; i < *count; i++) {
                        if (strcmp(networks[i].name, ifa->ifa_name) == 0) {
                            idx = i;
                            break;
                        }
                    }
                    
                    if (idx >= 0) {
                        strncpy(networks[idx].mac, mac_str, 17);
                        networks[idx].mac[17] = '\0';
                    } else if (*count < MAX_NETWORKS) {
                        strncpy(networks[*count].name, ifa->ifa_name, 31);
                        networks[*count].name[31] = '\0';
                        strncpy(networks[*count].mac, mac_str, 17);
                        networks[*count].mac[17] = '\0';
                        networks[*count].ipv4[0] = '\0';
                        networks[*count].ipv6[0] = '\0';
                        (*count)++;
                    }
                }
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            
            int idx = -1;
            for (int i = 0; i < *count; i++) {
                if (strcmp(networks[i].name, ifa->ifa_name) == 0) {
                    idx = i;
                    break;
                }
            }
            
            if (idx >= 0) {
                strncpy(networks[idx].ipv4, inet_ntoa(addr->sin_addr), 15);
                networks[idx].ipv4[15] = '\0';
            } else if (*count < MAX_NETWORKS) {
                strncpy(networks[*count].name, ifa->ifa_name, 31);
                networks[*count].name[31] = '\0';
                strncpy(networks[*count].ipv4, inet_ntoa(addr->sin_addr), 15);
                networks[*count].ipv4[15] = '\0';
                networks[*count].mac[0] = '\0';
                networks[*count].ipv6[0] = '\0';
                (*count)++;
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            char addr_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr6->sin6_addr, addr_str, INET6_ADDRSTRLEN);
            
            for (int i = 0; i < *count; i++) {
                if (strcmp(networks[i].name, ifa->ifa_name) == 0) {
                    strncpy(networks[i].ipv6, addr_str, 45);
                    networks[i].ipv6[45] = '\0';
                    break;
                }
            }
        }
    }
    
    freeifaddrs(ifaddr);
}

void format_bytes(unsigned long bytes, char *buf, size_t len) {
    const char *units = "BKMGTP";
    int unit_idx = 0;
    double size = bytes;
    
    while (size >= 1024 && unit_idx < 6) {
        size /= 1024;
        unit_idx++;
    }
    
    if (unit_idx == 0) {
        snprintf(buf, len, "%.0f B", size);
    } else {
        snprintf(buf, len, "%.1f %cB", size, units[unit_idx]);
    }
}

void format_uptime(long seconds, char *buf, size_t len) {
    long days = seconds / 86400;
    long hours = (seconds % 86400) / 3600;
    long mins = (seconds % 3600) / 60;
    
    if (days > 0) {
        snprintf(buf, len, "%ldd %ldh %ldm", days, hours, mins);
    } else if (hours > 0) {
        snprintf(buf, len, "%ldh %ldm", hours, mins);
    } else {
        snprintf(buf, len, "%ldm", mins);
    }
}
