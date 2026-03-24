#include "common.h"
#include "parson.h"
#include <stdarg.h>
#include <sys/utsname.h>
#include <errno.h>

typedef struct {
    char client_id[64];
    int interval;
    int quiet;
    char *urls[16];
    int url_count;
} ClientConfig;

ClientConfig config;

void generate_client_id(char *id, size_t len) {
    const char *chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    srand((unsigned int)time(NULL));
    for (size_t i = 0; i < len - 1; i++) {
        id[i] = chars[rand() % strlen(chars)];
    }
    id[len - 1] = '\0';
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

void get_system_info(ClientInfo *info) {
    struct utsname uts;
    uname(&uts);
    
    strncpy(info->hostname, uts.nodename, sizeof(info->hostname) - 1);
    strncpy(info->arch, uts.machine, sizeof(info->arch) - 1);
    strncpy(info->kernel, uts.release, sizeof(info->kernel) - 1);
    
    long uptime_sec;
    get_uptime(&uptime_sec);
    info->uptime = uptime_sec;
    
    get_load(&info->load1, &info->load5, &info->load15);
    
    get_memory(&info->memory_total, &info->memory_used);
    
    get_network_info(info->networks, &info->network_count);
    
    info->timestamp = time(NULL);
}

char *build_json(ClientInfo *info) {
    time_t now = time(NULL);
    struct tm *local_tm = localtime(&info->timestamp);
    char timestamp_str[64];
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%S%z", local_tm);
    
    size_t ts_len = strlen(timestamp_str);
    if (ts_len == 25 && (timestamp_str[ts_len-5] == '+' || timestamp_str[ts_len-5] == '-')) {
        timestamp_str[ts_len-4] = ':';
    }
    
    char escaped_hostname[512], escaped_arch[128], escaped_kernel[512], escaped_client_id[128];
    escape_json_string(info->hostname, escaped_hostname, sizeof(escaped_hostname));
    escape_json_string(info->arch, escaped_arch, sizeof(escaped_arch));
    escape_json_string(info->kernel, escaped_kernel, sizeof(escaped_kernel));
    escape_json_string(info->client_id, escaped_client_id, sizeof(escaped_client_id));
    
    char *result = malloc(8192);
    if (!result) return strdup("{}");
    
    int pos = 0;
    pos += snprintf(result + pos, 8192 - pos,
        "{\"client_id\":\"%s\",\"hostname\":\"%s\",\"arch\":\"%s\",\"kernel\":\"%s\","
        "\"uptime\":%ld,\"load1\":%.2f,\"load5\":%.2f,\"load15\":%.2f,"
        "\"memory_total\":%lu,\"memory_used\":%lu,\"timestamp\":\"%s\",\"network\":[",
        escaped_client_id, escaped_hostname, escaped_arch, escaped_kernel,
        info->uptime, info->load1, info->load5, info->load15,
        info->memory_total, info->memory_used, timestamp_str);
    
    for (int i = 0; i < info->network_count; i++) {
        char escaped_name[64], escaped_mac[32], escaped_ipv4[32], escaped_ipv6[64];
        escape_json_string(info->networks[i].name, escaped_name, sizeof(escaped_name));
        escape_json_string(info->networks[i].mac, escaped_mac, sizeof(escaped_mac));
        escape_json_string(info->networks[i].ipv4, escaped_ipv4, sizeof(escaped_ipv4));
        escape_json_string(info->networks[i].ipv6, escaped_ipv6, sizeof(escaped_ipv6));
        
        pos += snprintf(result + pos, 8192 - pos,
            "{\"name\":\"%s\",\"mac\":\"%s\",\"ipv4\":\"%s\",\"ipv6\":\"%s\"}%s",
            escaped_name, escaped_mac, escaped_ipv4, escaped_ipv6,
            i < info->network_count - 1 ? "," : "");
    }
    
    pos += snprintf(result + pos, 8192 - pos, "]}");
    
    return result;
}

int send_http_post(const char *url, const char *json_data) {
    char host[256] = "";
    int port = 80;
    char path[512] = "/";
    
    const char *url_ptr = url;
    if (strncmp(url, "http://", 7) == 0) {
        url_ptr = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        url_ptr = url + 8;
    }
    
    char *path_start = strchr(url_ptr, '/');
    if (path_start) {
        int host_len = path_start - url_ptr;
        strncpy(host, url_ptr, host_len);
        host[host_len] = '\0';
        strncpy(path, path_start, sizeof(path) - 1);
    } else {
        strncpy(host, url_ptr, sizeof(host) - 1);
        strcpy(path, "/");
    }
    
    char *port_start = strchr(host, ':');
    if (port_start) {
        *port_start = '\0';
        port = atoi(port_start + 1);
        if (port == 0) port = 80;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        close(sock);
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, server->h_addr_list[0], server->h_length);
    addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    char request[65536];
    int header_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
path, host, strlen(json_data));
    
    memcpy(request + header_len, json_data, strlen(json_data));
    memcpy(request + header_len, json_data, strlen(json_data));
    int total_len = header_len + strlen(json_data);
    
    if (!config.quiet) {
        fprintf(stderr, "[DEBUG] Request: POST %s Host: %s\n", path, host);
        //fprintf(stderr, "[DEBUG] Header length: %d, JSON length: %zu, Total: %d\n", header_len, strlen(json_data), total_len);
    }
    
    if (send(sock, request, total_len, 0) < 0) {
        close(sock);
        return -1;
    }
    
    char response[1024];
    int n = recv(sock, response, sizeof(response) - 1, 0);
    
    //if (!config.quiet) {
    //    fprintf(stderr, "[DEBUG] Response bytes: %d\n", n);
    //}
    
    close(sock);
    
    if (n > 0) {
        response[n] = '\0';
        //if (!config.quiet) {
        //    fprintf(stderr, "[DEBUG] Response: %.200s\n", response);
        //}
        if (strstr(response, "200") != NULL || strstr(response, "ok") != NULL) {
            return 0;
        }
    }
    
    return -1;
}

int main(int argc, char *argv[]) {
    strcpy(config.client_id, "");
    config.interval = 60;
    config.quiet = 0;
    config.url_count = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            strncpy(config.client_id, argv[++i], sizeof(config.client_id) - 1);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            config.interval = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            config.interval = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--quiet") == 0) {
            config.quiet = 1;
        } else if (argv[i][0] != '-') {
            if (config.url_count < 16) {
                config.urls[config.url_count++] = argv[i];
            }
        }
    }
    
    if (config.url_count == 0) {
        printf("Usage: %s --id <id> --interval <seconds> [--quiet] <url> [url...]\n", argv[0]);
        printf("Example: %s --id my-server -i 60 http://192.168.1.100:8080/systemmonitor/v1/put\n", argv[0]);
        return 1;
    }
    
    if (strlen(config.client_id) == 0) {
        generate_client_id(config.client_id, 7);
    }
    
    if (!config.quiet) {
        printf("Client ID: %s\n", config.client_id);
        printf("Interval: %d seconds\n", config.interval);
        printf("URLs: ");
        for (int i = 0; i < config.url_count; i++) {
            printf("%s ", config.urls[i]);
        }
        printf("\n");
        printf("Starting monitoring...\n");
    }
    
    while (1) {
        ClientInfo info;
        memset(&info, 0, sizeof(info));
        strncpy(info.client_id, config.client_id, sizeof(info.client_id) - 1);
        
        get_system_info(&info);
        
        if (!config.quiet) {
            char mem_used_str[32], mem_total_str[32];
            format_bytes(info.memory_used, mem_used_str, sizeof(mem_used_str));
            format_bytes(info.memory_total, mem_total_str, sizeof(mem_total_str));
            printf("Collected: hostname=%s, load=%.2f, memory=%s/%s\n",
                info.hostname, info.load1, mem_used_str, mem_total_str);
        }
        
        char *json_data = build_json(&info);
        
        for (int i = 0; i < config.url_count; i++) {
            int ret = send_http_post(config.urls[i], json_data);
            if (ret == 0 && !config.quiet) {
                printf("Sent to %s: ok\n", config.urls[i]);
            } else if (ret != 0 && !config.quiet) {
                printf("Failed to send to %s\n", config.urls[i]);
            }
        }
        
        free(json_data);
        
        sleep(config.interval);
    }
    
    return 0;
}
