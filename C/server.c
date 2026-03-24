#include "common.h"
#include "parson.h"
#include "chart.h"

typedef struct {
    ClientHistory clients[MAX_CLIENTS];
    int client_count;
    pthread_mutex_t mutex;
    int quiet;
    char url_prefix[128];
} Server;

Server server = {.client_count = 0, .quiet = 0, .url_prefix = ""};

void server_log(const char *fmt, ...) {
    if (server.quiet) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void escape_json_string(const char *src, char *dst, size_t len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < len - 1; i++) {
        switch (src[i]) {
            case '"':  if (j + 2 < len) { dst[j++] = '\\'; dst[j++] = '"'; } break;
            case '\\': if (j + 2 < len) { dst[j++] = '\\'; dst[j++] = '\\'; } break;
            case '\n': if (j + 2 < len) { dst[j++] = '\\'; dst[j++] = 'n'; } break;
            case '\r': if (j + 2 < len) { dst[j++] = '\\'; dst[j++] = 'r'; } break;
            case '\t': if (j + 2 < len) { dst[j++] = '\\'; dst[j++] = 't'; } break;
            default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

void generate_html(char **output, size_t *len) {
    char *buf = malloc(BUFFER_SIZE * 4);
    if (!buf) return;
    
    int pos = 0;
    
    pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<title>System Monitor</title>\n"
        "<style>\n"
        "* { box-sizing: border-box; margin: 0; padding: 0; }\n"
        "body { font-family: Arial, sans-serif; background: #f5f5f5; height: 100vh; display: flex; flex-direction: column; }\n"
        "header { background: #333; color: white; padding: 15px 20px; display: flex; justify-content: space-between; align-items: center; }\n"
        "header h1 { margin: 0; font-size: 20px; }\n"
        ".auto-refresh-btn { background: #4CAF50; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; font-size: 14px; }\n"
        ".auto-refresh-btn.off { background: #666; }\n"
        ".container { display: flex; flex: 1; overflow: hidden; }\n"
        ".sidebar { width: 280px; background: white; border-right: 1px solid #ddd; overflow-y: auto; padding: 10px; }\n"
        ".client-item { padding: 12px; margin: 5px 0; border-radius: 6px; cursor: pointer; border: 2px solid transparent; transition: all 0.2s; }\n"
        ".client-item:hover { background: #f0f0f0; }\n"
        ".client-item.active { border-color: #4CAF50; background: #e8f5e9; }\n"
        ".client-item.online { border-left: 4px solid #4CAF50; }\n"
        ".client-item.offline { border-left: 4px solid #f44336; opacity: 0.7; }\n"
        ".client-item h3 { font-size: 14px; margin-bottom: 5px; }\n"
        ".client-item .status { font-size: 12px; padding: 2px 6px; border-radius: 3px; }\n"
        ".client-item .status.online { background: #4CAF50; color: white; }\n"
        ".client-item .status.offline { background: #f44336; color: white; }\n"
        ".main { flex: 1; padding: 20px; overflow-y: auto; }\n"
        ".detail { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); display: none; }\n"
        ".detail.active { display: block; }\n"
        ".detail h2 { color: #333; margin-bottom: 15px; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }\n"
        "table { border-collapse: collapse; width: 100%%; margin: 15px 0; }\n"
        "th, td { border: 1px solid #ddd; padding: 10px; text-align: left; }\n"
        "th { background: #4CAF50; color: white; }\n"
        ".network-table { font-size: 14px; margin-top: 15px; }\n"
        ".network-table th { background: #666; }\n"
        ".chart-container { margin: 20px 0; }\n"
        ".chart-container h3 { color: #333; margin-bottom: 10px; }\n"
        ".chart-wrapper { position: relative; height: 200px; width: 100%%; }\n"
        ".empty-state { text-align: center; padding: 50px; color: #666; }\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "<header>\n"
        "<h1>System Monitor</h1>\n"
        "<button id=\"autoRefreshBtn\" class=\"auto-refresh-btn\" onclick=\"toggleAutoRefresh()\">Auto Refresh: ON</button>\n"
        "</header>\n"
        "<div class=\"container\">\n"
        "<div class=\"sidebar\">\n");
    
    pthread_mutex_lock(&server.mutex);
    for (int i = 0; i < server.client_count; i++) {
        ClientHistory *ch = &server.clients[i];
        time_t now = time(NULL);
        int is_online = (now - ch->last_seen) < OFFLINE_TIMEOUT;
        char *status_class = is_online ? "online" : "offline";
        char *status_text = is_online ? "Online" : "Offline";
        char primary_ip[64] = "";
        if (ch->latest_info.network_count > 0) {
            strncpy(primary_ip, ch->latest_info.networks[0].ipv4, sizeof(primary_ip) - 1);
        }
        
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
            "<div class=\"client-item %s\" onclick=\"showDetail('%s')\">\n"
            "<h3>%s</h3>\n"
            "<span class=\"status %s\">%s</span>\n"
            "<div style=\"font-size:12px;color:#666;\">%s</div>\n"
            "</div>\n",
            status_class, ch->latest_info.client_id,
            ch->latest_info.client_id, status_class, status_text, primary_ip);
    }
    pthread_mutex_unlock(&server.mutex);
    
    if (server.client_count == 0) {
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
            "<p style=\"padding:20px;color:#666;\">No clients connected</p>\n");
    }
    
    pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
        "</div>\n"
        "<div class=\"main\">\n");
    
    pthread_mutex_lock(&server.mutex);
    for (int i = 0; i < server.client_count; i++) {
        ClientHistory *ch = &server.clients[i];
        time_t now = time(NULL);
        int is_online = (now - ch->last_seen) < OFFLINE_TIMEOUT;
        char *status_class = is_online ? "online" : "offline";
        char *status_text = is_online ? "Online" : "Offline";
        char uptime_str[64], mem_used_str[32], mem_total_str[32], mem_pct_str[16];
        format_uptime(ch->latest_info.uptime, uptime_str, sizeof(uptime_str));
        format_bytes(ch->latest_info.memory_used, mem_used_str, sizeof(mem_used_str));
        format_bytes(ch->latest_info.memory_total, mem_total_str, sizeof(mem_total_str));
        double mem_pct = ch->latest_info.memory_total > 0 ? 
            (double)ch->latest_info.memory_used / ch->latest_info.memory_total * 100 : 0;
        snprintf(mem_pct_str, sizeof(mem_pct_str), "%.1f", mem_pct);
        
        char timestamp_str[64];
        if (ch->latest_info.timestamp > 0) {
            struct tm *ts_tm = localtime(&ch->latest_info.timestamp);
            strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%S%z", ts_tm);
            size_t ts_len = strlen(timestamp_str);
            if (ts_len == 25 && (timestamp_str[ts_len-5] == '+' || timestamp_str[ts_len-5] == '-')) {
                timestamp_str[ts_len-4] = ':';
            }
        } else {
            strcpy(timestamp_str, "");
        }
        
        struct tm *tm_info = localtime(&ch->last_seen);
        char last_seen_str[64];
        strftime(last_seen_str, sizeof(last_seen_str), "%Y-%m-%dT%H:%M:%S%z", tm_info);
        size_t ls_len = strlen(last_seen_str);
        if (ls_len == 25 && (last_seen_str[ls_len-5] == '+' || last_seen_str[ls_len-5] == '-')) {
            last_seen_str[ls_len-4] = ':';
        }
        
        time_t diff = now - ch->last_seen;
        char last_seen_ago[32];
        if (diff < 0) diff = 0;
        if (diff >= 86400) snprintf(last_seen_ago, sizeof(last_seen_ago), "%ldd", diff / 86400);
        else if (diff >= 3600) snprintf(last_seen_ago, sizeof(last_seen_ago), "%ldh", diff / 3600);
        else if (diff >= 60) snprintf(last_seen_ago, sizeof(last_seen_ago), "%ldm", diff / 60);
        else snprintf(last_seen_ago, sizeof(last_seen_ago), "%lds", (long)diff);
        
        char primary_ip[64] = "";
        if (ch->latest_info.network_count > 0) {
            strncpy(primary_ip, ch->latest_info.networks[0].ipv4, sizeof(primary_ip) - 1);
        }
        
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
            "<div id=\"detail-%s\" class=\"detail\">\n"
            "<h2>%s (%s)</h2>\n"
            "<span class=\"status %s\">%s</span>\n"
            "<span style=\"margin-left:10px;color:#666;\">Last Seen: %s (%s ago)</span>\n"
            "<table>\n"
            "<tr><th>Property</th><th>Value</th></tr>\n"
            "<tr><td>IP Address</td><td>%s</td></tr>\n"
            "<tr><td>Architecture</td><td>%s</td></tr>\n"
            "<tr><td>Kernel</td><td>%s</td></tr>\n"
            "<tr><td>Uptime</td><td>%s</td></tr>\n"
            "<tr><td>Load (1/5/15 min)</td><td>%.2f / %.2f / %.2f</td></tr>\n"
            "<tr><td>Memory</td><td>%s / %s (%s%%)</td></tr>\n"
            "<tr><td>Timestamp</td><td>%s</td></tr>\n"
            "</table>\n",
            ch->latest_info.client_id,
            ch->latest_info.hostname, ch->latest_info.client_id,
            status_class, status_text, last_seen_str, last_seen_ago,
            primary_ip,
            ch->latest_info.arch,
            ch->latest_info.kernel,
            uptime_str,
            ch->latest_info.load1, ch->latest_info.load5, ch->latest_info.load15,
            mem_used_str, mem_total_str, mem_pct_str, timestamp_str);
        
        if (ch->latest_info.network_count > 0) {
            pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
                "<h3>Network Interfaces</h3>\n"
                "<table class=\"network-table\">\n"
                "<tr><th>Name</th><th>MAC</th><th>IPv4</th><th>IPv6</th></tr>\n");
            for (int j = 0; j < ch->latest_info.network_count; j++) {
                NetworkInfo *net = &ch->latest_info.networks[j];
                pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
                    "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
                    net->name, net->mac, net->ipv4, net->ipv6);
            }
            pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos, "</table>\n");
        }
        
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
            "<div class=\"chart-container\">\n"
            "<h3>Load History</h3>\n"
            "<div class=\"chart-wrapper\"><canvas id=\"load-chart-%s\"></canvas></div>\n"
            "</div>\n"
            "<div class=\"chart-container\">\n"
            "<h3>Memory History</h3>\n"
            "<div class=\"chart-wrapper\"><canvas id=\"mem-chart-%s\"></canvas></div>\n"
            "</div>\n"
            "</div>\n",
            ch->latest_info.client_id, ch->latest_info.client_id);
    }
    pthread_mutex_unlock(&server.mutex);
    
    if (server.client_count == 0) {
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
            "<div id=\"empty-state\" class=\"empty-state\">\n"
            "<h2>Select a client to view details</h2>\n"
            "</div>\n");
    } else {
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
            "<div id=\"empty-state\" class=\"empty-state\" style=\"display:none\">\n"
            "<h2>Select a client to view details</h2>\n"
            "</div>\n");
    }
    
    pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
        "</div>\n"
        "</div>\n"
        "<script>\n"
        "var chartScript = document.createElement('script');\n"
        "chartScript.textContent = %s;\n"
        "document.head.appendChild(chartScript);\n"
        "</script>\n"
        "<script>\n"
        "var clients = [\n", chart_str);
    
    pthread_mutex_lock(&server.mutex);
    for (int i = 0; i < server.client_count; i++) {
        ClientHistory *ch = &server.clients[i];
        time_t now = time(NULL);
        int is_online = (now - ch->last_seen) < OFFLINE_TIMEOUT;
        
        char escaped_hostname[512], escaped_arch[128], escaped_kernel[512], escaped_client_id[128];
        escape_json_string(ch->latest_info.hostname, escaped_hostname, sizeof(escaped_hostname));
        escape_json_string(ch->latest_info.arch, escaped_arch, sizeof(escaped_arch));
        escape_json_string(ch->latest_info.kernel, escaped_kernel, sizeof(escaped_kernel));
        escape_json_string(ch->latest_info.client_id, escaped_client_id, sizeof(escaped_client_id));
        
        char timestamp_str[64], last_seen_str[64], history_time_str[64];
        struct tm *tm_info;
        
        if (ch->latest_info.timestamp > 0) {
            tm_info = localtime(&ch->latest_info.timestamp);
            strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%S%z", tm_info);
            size_t ts_len = strlen(timestamp_str);
            if (ts_len == 25 && (timestamp_str[ts_len-5] == '+' || timestamp_str[ts_len-5] == '-')) {
                timestamp_str[ts_len-4] = ':';
            }
        } else {
            strcpy(timestamp_str, "");
        }
        
        tm_info = localtime(&ch->last_seen);
        strftime(last_seen_str, sizeof(last_seen_str), "%Y-%m-%dT%H:%M:%S%z", tm_info);
        size_t ls_len = strlen(last_seen_str);
        if (ls_len == 25 && (last_seen_str[ls_len-5] == '+' || last_seen_str[ls_len-5] == '-')) {
            last_seen_str[ls_len-4] = ':';
        }
        
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
            "{\"client_id\":\"%s\",\"hostname\":\"%s\",\"arch\":\"%s\",\"kernel\":\"%s\","
            "\"uptime\":%ld,\"load1\":%.2f,\"load5\":%.2f,\"load15\":%.2f,"
            "\"memory_total\":%lu,\"memory_used\":%lu,\"timestamp\":\"%s\","
            "\"ip\":\"%s\",\"is_online\":%s,\"last_seen\":\"%s\","
            "\"load_history\":[",
            escaped_client_id, escaped_hostname, escaped_arch, escaped_kernel,
            ch->latest_info.uptime, ch->latest_info.load1, ch->latest_info.load5, ch->latest_info.load15,
            ch->latest_info.memory_total, ch->latest_info.memory_used, 
            timestamp_str,
            ch->latest_info.network_count > 0 ? ch->latest_info.networks[0].ipv4 : "",
            is_online ? "true" : "false",
            last_seen_str);
        
        for (int j = 0; j < ch->history_count; j++) {
            pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos, "%.2f%s", 
                ch->load_history[j], j < ch->history_count - 1 ? "," : "");
        }
        
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos, "],\"mem_history\":[");
        for (int j = 0; j < ch->history_count; j++) {
            pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos, "%lu%s",
                ch->mem_history[j], j < ch->history_count - 1 ? "," : "");
        }
        
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos, "],\"time_history\":[");
        for (int j = 0; j < ch->history_count; j++) {
            tm_info = localtime(&ch->time_history[j]);
            strftime(history_time_str, sizeof(history_time_str), "%Y-%m-%dT%H:%M:%S%z", tm_info);
            pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos, "\"%s\"%s",
                history_time_str, j < ch->history_count - 1 ? "," : "");
        }
        
        pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos, "]}%s\n", i < server.client_count - 1 ? "," : "");
    }
    pthread_mutex_unlock(&server.mutex);
    
    pos += snprintf(buf + pos, BUFFER_SIZE * 4 - pos,
        "];\n"
        "var loadCharts = {};\n"
        "var autoRefreshEnabled = true;\n"
        "var refreshIntervalId = null;\n"
        "\n"
        "function toggleAutoRefresh() {\n"
        "    autoRefreshEnabled = !autoRefreshEnabled;\n"
        "    var btn = document.getElementById('autoRefreshBtn');\n"
        "    if (autoRefreshEnabled) {\n"
        "        btn.textContent = 'Auto Refresh: ON';\n"
        "        btn.classList.remove('off');\n"
        "        startRefreshInterval();\n"
        "    } else {\n"
        "        btn.textContent = 'Auto Refresh: OFF';\n"
        "        btn.classList.add('off');\n"
        "        stopRefreshInterval();\n"
        "    }\n"
        "}\n"
        "\n"
        "function startRefreshInterval() {\n"
        "    if (refreshIntervalId) clearInterval(refreshIntervalId);\n"
        "    refreshIntervalId = setInterval(doRefresh, 1000);\n"
        "}\n"
        "\n"
        "function stopRefreshInterval() {\n"
        "    if (refreshIntervalId) {\n"
        "        clearInterval(refreshIntervalId);\n"
        "        refreshIntervalId = null;\n"
        "    }\n"
        "}\n"
        "\n"
        "function doRefresh() {\n"
        "    var urlBase = window.location.pathname.replace('/index.html', '');\n"
        "    fetch(urlBase + '/data')\n"
        "        .then(function(response) { return response.json(); })\n"
        "        .then(function(newClients) {\n"
        "            clients = newClients;\n"
        "            var oldSidebar = document.querySelector('.sidebar');\n"
        "            var newSidebarHTML = '';\n"
        "            for (var i = 0; i < newClients.length; i++) {\n"
        "                var c = newClients[i];\n"
        "                var statusClass = c.is_online ? 'online' : 'offline';\n"
        "                var statusText = c.is_online ? 'Online' : 'Offline';\n"
        "                newSidebarHTML += '<div class=\"client-item ' + statusClass + '\" onclick=\"showDetail(\\'' + c.client_id + '\\')\"><h3>' + c.client_id + '</h3><span class=\"status ' + statusClass + '\">' + statusText + '</span><div style=\"font-size:12px;color:#666;\">' + c.ip + '</div></div>';\n"
        "            }\n"
        "            oldSidebar.innerHTML = newSidebarHTML || '<p style=\"padding:20px;color:#666;\">No clients connected</p>';\n"
        "            var activeDetail = document.querySelector('.detail.active');\n"
        "            if (activeDetail) {\n"
        "                var clientId = activeDetail.id.replace('detail-', '');\n"
        "                var client = clients.find(function(c) { return c.client_id === clientId; });\n"
        "                if (!client) {\n"
        "                    if (clients.length > 0) { showDetail(clients[0].client_id); }\n"
        "                } else {\n"
        "                    var h2 = activeDetail.querySelector('h2');\n"
        "                    var statusSpan = activeDetail.querySelector('.detail .status');\n"
        "                    var lastSeenSpan = activeDetail.querySelector('.detail span:last-of-type');\n"
        "                    if (h2) h2.textContent = client.hostname + ' (' + client.client_id + ')';\n"
        "                    if (statusSpan) {\n"
        "                        statusSpan.className = 'status ' + (client.is_online ? 'online' : 'offline');\n"
        "                        statusSpan.textContent = client.is_online ? 'Online' : 'Offline';\n"
        "                    }\n"
        "                    var agoText = '';\n"
        "                    var diffSecs = Math.floor((new Date() - new Date(client.last_seen)) / 1000);\n"
        "                    if (diffSecs >= 86400) agoText = Math.floor(diffSecs / 86400) + 'd';\n"
        "                    else if (diffSecs >= 3600) agoText = Math.floor(diffSecs / 3600) + 'h';\n"
        "                    else if (diffSecs >= 60) agoText = Math.floor(diffSecs / 60) + 'm';\n"
        "                    else agoText = diffSecs + 's';\n"
        "                    if (lastSeenSpan) lastSeenSpan.textContent = 'Last Seen: ' + client.last_seen + ' (' + agoText + ' ago)';\n"
        "                    var rows = activeDetail.querySelectorAll('table:first-of-type tr');\n"
        "                    rows.forEach(function(row) {\n"
        "                        var td = row.querySelector('td');\n"
        "                        if (!td) return;\n"
        "                        var label = td.textContent;\n"
        "                        if (label === 'IP Address') row.querySelector('td:last-child').textContent = client.ip || '';\n"
        "                        else if (label === 'Architecture') row.querySelector('td:last-child').textContent = client.arch || '';\n"
        "                        else if (label === 'Kernel') row.querySelector('td:last-child').textContent = client.kernel || '';\n"
        "                        else if (label === 'Uptime') {\n"
        "                            var secs = client.uptime;\n"
        "                            var days = Math.floor(secs / 86400);\n"
        "                            var hours = Math.floor((secs %% 86400) / 3600);\n"
        "                            var mins = Math.floor((secs %% 3600) / 60);\n"
        "                            var uptimeStr = days > 0 ? days + 'd ' + hours + 'h ' + mins + 'm' : hours > 0 ? hours + 'h ' + mins + 'm' : mins + 'm';\n"
        "                            row.querySelector('td:last-child').textContent = uptimeStr;\n"
        "                        }\n"
        "                        else if (label === 'Load (1/5/15 min)') {\n"
        "                            row.querySelector('td:last-child').textContent = client.load1.toFixed(2) + ' / ' + client.load5.toFixed(2) + ' / ' + client.load15.toFixed(2);\n"
        "                        }\n"
        "                        else if (label === 'Memory') {\n"
        "                            var memUsed = (client.memory_used / (1024*1024)).toFixed(1);\n"
        "                            var memTotal = (client.memory_total / (1024*1024)).toFixed(1);\n"
        "                            var memPct = client.memory_total > 0 ? (client.memory_used / client.memory_total * 100).toFixed(1) : 0;\n"
        "                            row.querySelector('td:last-child').textContent = memUsed + ' MB / ' + memTotal + ' MB (' + memPct + '%%)';\n"
        "                        }\n"
        "                        else if (label === 'Timestamp') {\n"
        "                            row.querySelector('td:last-child').textContent = client.timestamp;\n"
        "                        }\n"
        "                    });\n"
        "                    var chartKey1 = clientId + '-load';\n"
        "                    var chartKey2 = clientId + '-mem';\n"
        "                    if (loadCharts[chartKey1]) loadCharts[chartKey1].destroy();\n"
        "                    if (loadCharts[chartKey2]) loadCharts[chartKey2].destroy();\n"
        "                    var canvases = activeDetail.querySelectorAll('canvas');\n"
        "                    for (var i = 0; i < canvases.length; i++) { canvases[i].remove(); }\n"
        "                    var chartWrappers = activeDetail.querySelectorAll('.chart-wrapper');\n"
        "                    for (var i = 0; i < chartWrappers.length; i++) {\n"
        "                        var newCanvas = document.createElement('canvas');\n"
        "                        newCanvas.id = (i === 0 ? 'load-chart-' : 'mem-chart-') + clientId;\n"
        "                        chartWrappers[i].appendChild(newCanvas);\n"
        "                    }\n"
        "                    loadCharts = {};\n"
        "                    createOrUpdateChart(clientId, 'load', client);\n"
        "                    createOrUpdateChart(clientId, 'mem', client);\n"
        "                }\n"
        "            }\n"
        "        })\n"
        "        .catch(function(err) { console.log('Refresh error:', err); });\n"
        "}\n"
        "\n"
        "function showDetail(clientId) {\n"
        "    document.querySelectorAll('.detail').forEach(function(el) { el.classList.remove('active'); });\n"
        "    document.querySelectorAll('.client-item').forEach(function(el) { el.classList.remove('active'); });\n"
        "    var detail = document.getElementById('detail-' + clientId);\n"
        "    if (detail) {\n"
        "        detail.classList.add('active');\n"
        "        document.getElementById('empty-state').style.display = 'none';\n"
        "    }\n"
        "    var clientItem = document.querySelector('.client-item[onclick*=\"' + clientId + '\"]');\n"
        "    if (clientItem) clientItem.classList.add('active');\n"
        "    var client = clients.find(function(c) { return c.client_id === clientId; });\n"
        "    if (client) {\n"
        "        createOrUpdateChart(clientId, 'load', client);\n"
        "        createOrUpdateChart(clientId, 'mem', client);\n"
        "    }\n"
        "}\n"
        "\n"
        "function formatTime(date) {\n"
        "    return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });\n"
        "}\n"
        "\n"
        "function createOrUpdateChart(clientId, type, client) {\n"
        "    var canvasId = type + '-chart-' + clientId;\n"
        "    var canvas = document.getElementById(canvasId);\n"
        "    if (!canvas) return;\n"
        "    var data, label, color, maxY;\n"
        "    if (type === 'load') {\n"
        "        data = client.load_history;\n"
        "        label = 'System Load';\n"
        "        color = '#4CAF50';\n"
        "        maxY = null;\n"
        "    } else {\n"
        "        data = client.mem_history;\n"
        "        label = 'Memory (MB)';\n"
        "        color = '#2196F3';\n"
        "        maxY = client.memory_total / (1024 * 1024);\n"
        "    }\n"
        "    var timeData = client.time_history || [];\n"
        "    var labels = timeData.map(function(d) { return formatTime(new Date(d)); });\n"
        "    var chartData = type === 'mem' ? data.map(function(v) { return (v / (1024*1024)).toFixed(0); }) : data;\n"
        "    var chartKey = clientId + '-' + type;\n"
        "    if (loadCharts[chartKey]) {\n"
        "        loadCharts[chartKey].data.labels = labels;\n"
        "        loadCharts[chartKey].data.datasets[0].data = chartData;\n"
        "        loadCharts[chartKey].update();\n"
        "    } else {\n"
        "        var ctx = canvas.getContext('2d');\n"
        "        loadCharts[chartKey] = new Chart(ctx, {\n"
        "            type: 'line',\n"
        "            data: {\n"
        "                labels: labels,\n"
        "                datasets: [{\n"
        "                    label: label,\n"
        "                    data: chartData,\n"
        "                    borderColor: color,\n"
        "                    backgroundColor: color + '33',\n"
        "                    tension: 0.1,\n"
        "                    fill: true,\n"
        "                    pointRadius: 0,\n"
        "                    borderWidth: 2\n"
        "                }]\n"
        "            },\n"
        "            options: {\n"
        "                responsive: true,\n"
        "                maintainAspectRatio: false,\n"
        "                animation: false,\n"
        "                scales: {\n"
        "                    x: { display: true, title: { display: true, text: 'Server Time' }, ticks: { maxTicksLimit: 10, autoSkip: true } },\n"
        "                    y: { display: true, beginAtZero: type === 'mem', suggestedMax: maxY }\n"
        "                },\n"
        "                plugins: { legend: { display: false } }\n"
        "            }\n"
        "        });\n"
        "    }\n"
        "}\n"
        "\n"
        "if (clients.length > 0) { showDetail(clients[0].client_id); }\n"
        "startRefreshInterval();\n"
        "</script>\n"
        "</body>\n"
        "</html>\n");
    
    *output = buf;
    *len = pos;
}

void handle_request(int client_fd, const char *method, const char *path) {
    char buf[4096];
    int is_data = strstr(path, "/data") != NULL;
    int is_index = strstr(path, "/index.html") != NULL;
    int is_root = strcmp(path, "/") == 0;
    
    if (is_root && strlen(server.url_prefix) > 0) {
        snprintf(buf, sizeof(buf),
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: /%s/index.html\r\n"
            "Content-Length: 0\r\n"
            "\r\n", server.url_prefix);
        send(client_fd, buf, strlen(buf), 0);
        return;
    }
    
    if (is_index) {
        char *html = NULL;
        size_t html_len;
        generate_html(&html, &html_len);
        
        snprintf(buf, sizeof(buf),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "\r\n", html_len);
        send(client_fd, buf, strlen(buf), 0);
        send(client_fd, html, html_len, 0);
        free(html);
        return;
    }
    
    if (is_data) {
        pthread_mutex_lock(&server.mutex);
        size_t json_size = BUFFER_SIZE * 4;
        char *json_buf = malloc(json_size);
        int pos = 0;
        
        pos += snprintf(json_buf + pos, json_size - pos, "[");
        for (int i = 0; i < server.client_count; i++) {
            ClientHistory *ch = &server.clients[i];
            time_t now = time(NULL);
            int is_online = (now - ch->last_seen) < OFFLINE_TIMEOUT;
            
            char timestamp_str[64], last_seen_str[64], history_time_str[64];
            struct tm *tm_info;
            
            if (ch->latest_info.timestamp > 0) {
                tm_info = localtime(&ch->latest_info.timestamp);
                strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%S%z", tm_info);
            } else {
                strcpy(timestamp_str, "");
            }
            
            tm_info = localtime(&ch->last_seen);
            strftime(last_seen_str, sizeof(last_seen_str), "%Y-%m-%dT%H:%M:%S%z", tm_info);
            
            pos += snprintf(json_buf + pos, json_size - pos,
                "{\"client_id\":\"%s\",\"hostname\":\"%s\",\"arch\":\"%s\",\"kernel\":\"%s\","
                "\"uptime\":%ld,\"load1\":%.2f,\"load5\":%.2f,\"load15\":%.2f,"
                "\"memory_total\":%lu,\"memory_used\":%lu,\"timestamp\":\"%s\","
                "\"ip\":\"%s\",\"is_online\":%s,\"last_seen\":\"%s\","
                "\"load_history\":[",
                ch->latest_info.client_id, ch->latest_info.hostname, ch->latest_info.arch, ch->latest_info.kernel,
                ch->latest_info.uptime, ch->latest_info.load1, ch->latest_info.load5, ch->latest_info.load15,
                ch->latest_info.memory_total, ch->latest_info.memory_used, timestamp_str,
                ch->latest_info.network_count > 0 ? ch->latest_info.networks[0].ipv4 : "",
                is_online ? "true" : "false", last_seen_str);
            
            for (int j = 0; j < ch->history_count; j++) {
                pos += snprintf(json_buf + pos, json_size - pos, "%.2f%s",
                    ch->load_history[j], j < ch->history_count - 1 ? "," : "");
            }
            
            pos += snprintf(json_buf + pos, json_size - pos, "],\"mem_history\":[");
            for (int j = 0; j < ch->history_count; j++) {
                pos += snprintf(json_buf + pos, json_size - pos, "%lu%s",
                    ch->mem_history[j], j < ch->history_count - 1 ? "," : "");
            }
            
            pos += snprintf(json_buf + pos, json_size - pos, "],\"time_history\":[");
            for (int j = 0; j < ch->history_count; j++) {
                char hist_time_str[64];
                struct tm *hist_tm = localtime(&ch->time_history[j]);
                strftime(hist_time_str, sizeof(hist_time_str), "%Y-%m-%dT%H:%M:%S%z", hist_tm);
                pos += snprintf(json_buf + pos, json_size - pos, "\"%s\"%s",
                    hist_time_str, j < ch->history_count - 1 ? "," : "");
            }
            
            pos += snprintf(json_buf + pos, json_size - pos, "]}%s\n", i < server.client_count - 1 ? "," : "");
        }
        pos += snprintf(json_buf + pos, json_size - pos, "]");
        pthread_mutex_unlock(&server.mutex);
        
        snprintf(buf, sizeof(buf),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "\r\n", pos);
        send(client_fd, buf, strlen(buf), 0);
        send(client_fd, json_buf, pos, 0);
        free(json_buf);
        return;
    }
    
    if (strcmp(method, "POST") == 0) {
        const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, not_found, strlen(not_found), 0);
        return;
    }
    
    const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    send(client_fd, not_found, strlen(not_found), 0);
}

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    
    char buf[BUFFER_SIZE];
    int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return NULL;
    }
    buf[n] = '\0';
    
    char method[16], path[256], version[16];
    if (sscanf(buf, "%15s %255s %15s", method, path, version) != 3) {
        close(client_fd);
        return NULL;
    }
    
    int content_length = 0;
    char *body = NULL;
    int body_len = 0;
    
    if (strcmp(method, "POST") == 0) {
        char *header_end = strstr(buf, "\r\n\r\n");
        if (header_end) {
            char *cl = strstr(buf, "Content-Length:");
            if (cl) {
                sscanf(cl, "Content-Length: %d", &content_length);
            }
            
            if (content_length > 0) {
                char *body_start = header_end + 4;
                int header_body_len = n - (body_start - buf);
                
                body = malloc(content_length + 1);
                if (body) {
                    body_len = 0;
                    if (header_body_len > 0) {
                        memcpy(body, body_start, header_body_len);
                        body_len = header_body_len;
                    }
                    
                    while (body_len < content_length) {
                        int r = recv(client_fd, body + body_len, content_length - body_len, 0);
                        if (r <= 0) break;
                        body_len += r;
                    }
                    body[body_len] = '\0';
                }
            }
        }
        
        if (body && body_len > 0) {
            JSON_Value *root_value = json_parse_string(body);
            if (root_value) {
                JSON_Object *root = json_value_get_object(root_value);
                
                ClientInfo info;
                strncpy(info.client_id, json_object_get_string(root, "client_id") ?: "", sizeof(info.client_id) - 1);
                strncpy(info.hostname, json_object_get_string(root, "hostname") ?: "", sizeof(info.hostname) - 1);
                strncpy(info.arch, json_object_get_string(root, "arch") ?: "", sizeof(info.arch) - 1);
                strncpy(info.kernel, json_object_get_string(root, "kernel") ?: "", sizeof(info.kernel) - 1);
                info.uptime = (long)json_object_get_number(root, "uptime");
                info.load1 = json_object_get_number(root, "load1");
                info.load5 = json_object_get_number(root, "load5");
                info.load15 = json_object_get_number(root, "load15");
                info.memory_total = (unsigned long)json_object_get_number(root, "memory_total");
                info.memory_used = (unsigned long)json_object_get_number(root, "memory_used");
                
                const char *ts_str = json_object_get_string(root, "timestamp");
                if (ts_str) {
                    struct tm tm_info;
                    memset(&tm_info, 0, sizeof(tm_info));
                    if (sscanf(ts_str, "%d-%d-%dT%d:%d:%d", 
                        &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday,
                        &tm_info.tm_hour, &tm_info.tm_min, &tm_info.tm_sec) == 6) {
                        tm_info.tm_year -= 1900;
                        tm_info.tm_mon -= 1;
                        info.timestamp = mktime(&tm_info);
                    } else {
                        info.timestamp = 0;
                    }
                } else {
                    info.timestamp = 0;
                }
                info.network_count = 0;
                
                JSON_Array *network_arr = json_object_get_array(root, "network");
                if (network_arr) {
                    size_t net_count = json_array_get_count(network_arr);
                    for (size_t j = 0; j < net_count && j < MAX_NETWORKS; j++) {
                        JSON_Object *net = json_array_get_object(network_arr, j);
                        if (net) {
                            strncpy(info.networks[j].name, json_object_get_string(net, "name") ?: "", 31);
                            strncpy(info.networks[j].mac, json_object_get_string(net, "mac") ?: "", 17);
                            strncpy(info.networks[j].ipv4, json_object_get_string(net, "ipv4") ?: "", 15);
                            strncpy(info.networks[j].ipv6, json_object_get_string(net, "ipv6") ?: "", 45);
                            info.network_count++;
                        }
                    }
                }
                
                pthread_mutex_lock(&server.mutex);
                
                int found = -1;
                for (int i = 0; i < server.client_count; i++) {
                    if (strcmp(server.clients[i].latest_info.client_id, info.client_id) == 0) {
                        found = i;
                        break;
                    }
                }
                
                if (found < 0 && server.client_count < MAX_CLIENTS) {
                    found = server.client_count++;
                    server.clients[found].history_count = 0;
                }
                
                if (found >= 0) {
                    ClientHistory *ch = &server.clients[found];
                    ch->last_seen = time(NULL);
                    ch->latest_info = info;
                    
                    if (ch->history_count >= HISTORY_SIZE) {
                        memmove(ch->load_history, ch->load_history + 1, (HISTORY_SIZE - 1) * sizeof(double));
                        memmove(ch->mem_history, ch->mem_history + 1, (HISTORY_SIZE - 1) * sizeof(unsigned long));
                        memmove(ch->time_history, ch->time_history + 1, (HISTORY_SIZE - 1) * sizeof(time_t));
                    } else {
                        ch->history_count++;
                    }
                    int idx = ch->history_count - 1;
                    ch->load_history[idx] = info.load1;
                    ch->mem_history[idx] = info.memory_used;
                    ch->time_history[idx] = time(NULL);
                    
                    if (!server.quiet) {
                        server_log("Received data from client: %s (%s)", info.hostname, info.client_id);
                    }
                }
                
                pthread_mutex_unlock(&server.mutex);
                json_value_free(root_value);
            }
            
            free(body);
            
            const char *response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 13\r\n\r\n{\"status\":\"ok\"}";
            send(client_fd, response, strlen(response), 0);
            close(client_fd);
            return NULL;
        }
    }
    
    handle_request(client_fd, method, path);
    
    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = 0;
    char url_prefix[128] = "";
    server.quiet = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            strncpy(url_prefix, argv[++i], sizeof(url_prefix) - 1);
        } else if (strcmp(argv[i], "--quiet") == 0) {
            server.quiet = 1;
        }
    }
    
    if (port == 0 || strlen(url_prefix) == 0) {
        printf("Usage: %s --port <port> --url <url-prefix> [--quiet]\n", argv[0]);
        return 1;
    }
    
    strncpy(server.url_prefix, url_prefix, sizeof(server.url_prefix) - 1);
    
    pthread_mutex_init(&server.mutex, NULL);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }
    
    if (!server.quiet) {
        server_log("Server starting on http://localhost:%d", port);
        server_log("PUT endpoint: http://localhost:%d/%s/put", port, url_prefix);
        server_log("Web UI: http://localhost:%d/%s/index.html", port, url_prefix);
    }
    
    while (1) {
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            perror("accept");
            free(client_fd);
            continue;
        }
        
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread);
    }
    
    close(server_fd);
    pthread_mutex_destroy(&server.mutex);
    return 0;
}
