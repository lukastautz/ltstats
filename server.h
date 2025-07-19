/*
Copyright 2025 Lukas Tautz

This file is part of LTstats <https://ltstats.de>.

LTstats is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef SERVER_H
#define SERVER_H

#include "include.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/sendfile.h>
#include "str.c"
#include "json-c/json.h"

#ifndef __dietlibc__
typedef void (*sighandler_t)(int);
#define CLONE_FILES 0x00000400
#endif

#define max(a, b) (a > b ? a : b)
#define min(a, b) (a < b ? a : b)
#define HTTP_BUF_SIZE 512 + max(sizeof(net_header_t) + sizeof(details_t) + (CONFIG_UPLOAD_MAX_N_STATS_AT_ONCE * sizeof(stats_t)), 60928)
#define CACHELINE 64
#define http_buf_compare(s1, s) (((uint16)len >= strlen(s1) + strlen(s)) && !memcmp(http_buf + strlen(s1), SLEN(s)))
#define SECTOR_SIZE 512

_Atomic uint32 children;

_Atomic bool *data_json_changed;
_Atomic uint32 *monitoring_reload;
_Atomic bool *admin_proc;

typedef struct {
    char token[33];
    char public_token[33];
    int fd;
    bool was_online;
    uint8 time_diff;
    details_t details;
    stats_t stats;
    uint64 rx_total;
    uint64 tx_total;
    uint64 sectors_read_total;
    uint64 sectors_written_total;
    bool public;
} monitor_details_t;

typedef struct {
    char token[33];
    char public_token[33];
    int fd;
    bool notification_sent[11]; // offline, cpu_usage, cpu_iowait, cpu_steal, ram_usage, swap_usage, disk_usage, net_rx_bps, net_tx_bps, disk_read_bps, disk_write_tx_bps
    json_object *name;
    json_object *monitoring_settings;
} notification_monitor_details_t;

typedef struct {
    int fd; // close if close_at >= current, and fd != -1
    time_t close_at;
} close_fds_t;

char http_buf[HTTP_BUF_SIZE];

monitor_details_t *details = NULL;
notification_monitor_details_t *notification_details = NULL;
close_fds_t *close_fds = NULL;
struct json_object *data_json = NULL, *monitors, *status_pages;
enum {
    SHOULD_HIDE_TOTAL_IO = 0,
    SHOULD_HIDE_TOTAL_TRAFFIC = 1,
    SHOULD_HIDE_KERNEL = 2,
    SHOULD_HIDE_CPU_MODEL = 3,
    SHOULD_HIDE_CPU_CORES = 4,
    SHOULD_HIDE_UPTIME = 5,
    SHOULD_HIDE_CPU_USAGE = 6,
    SHOULD_HIDE_CPU_IOWAIT = 7,
    SHOULD_HIDE_CPU_STEAL = 8,
    SHOULD_HIDE_RAM_SIZE = 9,
    SHOULD_HIDE_RAM_USAGE = 10,
    SHOULD_HIDE_SWAP_SIZE = 11,
    SHOULD_HIDE_SWAP_USAGE = 12,
    SHOULD_HIDE_DISK_SIZE = 13,
    SHOULD_HIDE_DISK_USAGE = 14,
    SHOULD_HIDE_NET = 15,
    SHOULD_HIDE_IO = 16
};
bool should_hide[17];
const char *admin_hash;
int client, status_page_fd, monitor_page_fd, admin_page_fd, favicon_ico_fd;
int32 len;

uint32 details_count = 0, close_fds_count = 0;

enum {
    PROC_WEB,
    PROC_NOTIFICATIONS
} proc;

#define TO_DOUBLE_FROM_TWO_UINTS(data) ((double)data##_before_decimal + ((double)data##_after_decimal / 100.0))

#endif