#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>

#define HISTORY_SIZE 128
#define OFFLINE_TIMEOUT 7200  // 2 hours in seconds
#define MAX_CLIENTS 1000
#define MAX_NETWORKS 16
#define BUFFER_SIZE 65536

typedef struct {
    char name[32];
    char mac[18];
    char ipv4[16];
    char ipv6[46];
} NetworkInfo;

typedef struct {
    char client_id[64];
    char hostname[256];
    char arch[64];
    char kernel[256];
    long uptime;
    double load1, load5, load15;
    unsigned long memory_total;
    unsigned long memory_used;
    time_t timestamp;
    NetworkInfo networks[MAX_NETWORKS];
    int network_count;
} ClientInfo;

typedef struct {
    time_t last_seen;
    int is_online;
    double load_history[HISTORY_SIZE];
    unsigned long mem_history[HISTORY_SIZE];
    time_t time_history[HISTORY_SIZE];
    int history_count;
    ClientInfo latest_info;
} ClientHistory;

void get_uptime(long *uptime_sec);
void get_load(double *load1, double *load5, double *load15);
void get_memory(unsigned long *total, unsigned long *used);
void get_network_info(NetworkInfo *networks, int *count);
void format_bytes(unsigned long bytes, char *buf, size_t len);
void format_uptime(long seconds, char *buf, size_t len);
int starts_with(const char *str, const char *prefix);
void trim(char *str);

#endif
