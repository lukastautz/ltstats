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

#include "include.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/sysinfo.h>
#include <netinet/tcp.h>
#include "BearSSL/inc/bearssl.h"
#include "config.h"
#include "TA.h"
#include "str.c"

typedef struct PACKED {
    uint64 user;
    uint64 nice;
    uint64 system;
    uint64 idle;
    uint64 iowait;
    uint64 irq;
    uint64 softirq;
    uint64 steal;
    uint64 guest;
    uint64 guest_nice;
    uint64 work; /* = user+nice+system+(soft)irq+guest+guest_nice */
    uint64 total;
} jiffies_spent_t;

char file_buf[49152], http_buf[512 + sizeof(net_header_t) + sizeof(details_t) + (CONFIG_UPLOAD_MAX_N_STATS_AT_ONCE * sizeof(stats_t))], response_buf[1024], iobuf[BR_SSL_BUFSIZE_BIDI], *host;
details_t details;
stats_t stats[CONFIG_MAX_CACHED];
uint32 stats_count = 0, stats_pos = (uint32)-1;
uint16 http_req_len;
stats_t *current_stats;
net_header_t header;
jiffies_spent_t cpu_start, cpu_end;
uint64 net_rx_start, net_tx_start, net_rx_end, net_tx_end, cpu_total_diff, cpu_iowait_diff, cpu_steal_diff, cpu_work_diff, disk_sectors_read_start, disk_sectors_written_start, disk_sectors_read_end, disk_sectors_written_end;
uint8 measure_every_n_seconds = CONFIG_MEASURE_EVERY_N_SECONDS;
br_ssl_client_context client_context;
br_x509_minimal_context minimal_context;
br_sslio_context sslio_context;
char **mount_paths = NULL;
char *mount_paths_default[] = { "/", NULL };

#define IN_READ(ptr) (ptr < (file_buf + read_len))

#define DOUBLE_TO_TWO_UINT8(name, v) \
    { \
        double tmp = v; \
        name##_before_decimal = (uint8)tmp; \
        name##_after_decimal = (uint8)((tmp - (double)((uint8)tmp)) * 100.0); \
    }

#define TWO_UINT8_ZERO(name) name##_before_decimal = name##_after_decimal = 0

#define OPEN_READ(name, what_when_error, min_length) \
    int read_len = 0; \
    { \
        int fd = open(name, O_RDONLY), tmp; \
        if (fd == -1) { \
            what_when_error \
        } \
        while (read_len < (int)(sizeof(file_buf) - 1) && (tmp = read(fd, file_buf + read_len, sizeof(file_buf) - 1 - read_len)) > 0) \
            read_len += tmp; \
        close(fd); \
        if (tmp == -1 || read_len < min_length) { \
            what_when_error \
        } \
        file_buf[read_len] = '\0'; \
    }

void get_current_jiffies(jiffies_spent_t *dest) {
    dest->total = 0;
    OPEN_READ("/proc/stat", return;, 64)
    uint64 tmp;
    char *ptr = file_buf + strlen("cpu");
    while (isspace(*++ptr) && IN_READ(ptr));
    if (!IN_READ(ptr))
        return;
    for (uint8 i = 0; i < 10; ++i) {
        tmp = (uint64)strtoull(ptr, NULL, 10);
        dest->total += tmp;
        memcpy(((uint64 *)dest) + i, &tmp, 8);
        while (!isspace(*++ptr) && IN_READ(ptr));
        if (!IN_READ(ptr)) {
            dest->total = 0;
            return;
        }
        while (isspace(*++ptr) && IN_READ(ptr));
        if (!IN_READ(ptr)) {
            dest->total = 0;
            return;
        }
    }
    dest->work = dest->user + dest->nice + dest->system + dest->guest + dest->guest_nice + dest->irq + dest->softirq;
}

void get_network_stats(uint64 *rx, uint64 *tx) {
    *rx = 0; *tx = 0;
    OPEN_READ("/proc/net/dev", return;, 64)
    char *ptr = file_buf, *line_start, *field;
    uint8 pos, line_count = 0;
    while (line_count < 2 && IN_READ(ptr)) { // skip header
        if (*ptr == '\n')
            ++line_count;
        ++ptr;
    }
    while (IN_READ(ptr)) {
        line_start = ptr;
        while (*ptr != '\n' && IN_READ(ptr)) 
            ++ptr;
        if (!IN_READ(ptr)) // this can't occur as it should be newline-terminated
            break;
        *ptr = '\0';
        ++ptr;
        if (!strstr(line_start, "lo:")) {
            field = line_start;
            while (isspace(*field) && *field)
                ++field;
            if (!*field || (*field != 'w' && *field != 'e')) // skip non-physical interfaces
                continue;
            while (*field != ':' && *field)
                ++field;
            if (!*field)
                continue;
            *field = '\0';
            if (strchr(line_start, '.')) // VLAN device
                continue;
            ++field;
            while (isspace(*field))
                ++field;
            *rx += (uint64)strtoull(field, NULL, 10);
            pos = 1;
            while (pos < 9) {
                while (!isspace(*field) && *field)
                    ++field;
                if (!*field)
                    break;
                while (isspace(*field))
                    ++field;
                ++pos;
            }
            if (pos == 9)
                *tx += (uint64)strtoull(field, NULL, 10);
        }
    }
}

void get_disk_stats(uint64 *sectors_read, uint64 *sectors_written) {
    *sectors_read = *sectors_written = 0;
    OPEN_READ("/proc/diskstats", return;, 32)
    char *ptr = file_buf, *device_name;
    while (IN_READ(ptr)) {
        for (uint8 i = 0; i < 3; ++i) {
            while (isspace(*ptr) && IN_READ(ptr)) ++ptr;
            if (!IN_READ(ptr))
                return;
            if (i == 2)
                device_name = ptr;
            while (!isspace(*ptr) && IN_READ(ptr)) ++ptr;
            if (!IN_READ(ptr))
                return;
        }
        *ptr = '\0';
        bool include = false;
        if (!memcmp(device_name, SLEN("sd")) || !memcmp(device_name, SLEN("vd")) || !memcmp(device_name, SLEN("hd")) || !memcmp(device_name, SLEN("fd")))
            include = (isalpha(device_name[2]) && (!device_name[3] || (isalpha(device_name[3]) && !device_name[4])));
        else if (!memcmp(device_name, SLEN("nvme")) || !memcmp(device_name, SLEN("mmcblk")) || !memcmp(device_name, SLEN("sata"))) // sata is not standard but seems to be used on some systems
            include = strchr(device_name + strlen("nvme"), 'p') == NULL; // only of the main disk, not partitions
        ++ptr;
        if (include) {
            for (uint8 i = 1; i < 3; ++i) {
                while (isspace(*ptr) && IN_READ(ptr)) ++ptr;
                if (!IN_READ(ptr))
                    return;
                while (!isspace(*ptr) && IN_READ(ptr)) ++ptr;
                if (!IN_READ(ptr))
                    return;
            }
            while (isspace(*ptr) && IN_READ(ptr)) ++ptr;
            if (!IN_READ(ptr))
                return;
            *sectors_read += (uint64)strtoull(ptr, &ptr, 10);
            for (uint8 i = 4; i < 7; ++i) {
                while (isspace(*ptr) && IN_READ(ptr)) ++ptr;
                if (!IN_READ(ptr))
                    return;
                while (!isspace(*ptr) && IN_READ(ptr)) ++ptr;
                if (!IN_READ(ptr))
                    return;
            }
            while (isspace(*ptr) && IN_READ(ptr)) ++ptr;
            if (!IN_READ(ptr))
                return;
            *sectors_written += (uint64)strtoull(ptr, &ptr, 10);
        }
        while (IN_READ(ptr) && *ptr != '\n') ++ptr;
        if (!IN_READ(ptr))
            return;
        if (*ptr == '\n') ++ptr;
    }
}

uint32 get_uptime(void) { // sysinfo would be more efficient, but it doesn't relay the available memory information (si_mem_available()), so the agent reads from procfs files (and only using sysinfo for the uptime seems not efficient either)
    OPEN_READ("/proc/uptime", return 0;, 3)
    return (uint32)strtoul(file_buf, NULL, 10);
}

void copy_cpu_model(void) {
    details.cpu_model_len = 0;
    OPEN_READ("/proc/cpuinfo", return;, 64)
    char *ptr = file_buf;
    if ((ptr = strstr(file_buf, "model name")) && (ptr = strstr(ptr, ": "))) {
        ptr += 2;
        char *end = strchr(ptr, '\n');
        if (end) {
            uint8 len = end - ptr;
            if (len > sizeof(details.cpu_model))
                len = sizeof(details.cpu_model);
            memcpy(details.cpu_model, ptr, len);
            details.cpu_model_len = len;
        }
    }
}

void copy_linux_version(void) {
    struct utsname un;
    if (!uname(&un)) {
        details.linux_version_len = strlen(un.release);
        if (details.linux_version_len > sizeof(details.linux_version))
            details.linux_version_len = sizeof(details.linux_version);
        memcpy(details.linux_version, un.release, details.linux_version_len);
    } else {
        memcpy(details.linux_version, "Unknown", sizeof("Unknown"));
        details.linux_version_len = strlen("Unknown");
    }
}

#define PROC_MEMINFO_KB 1024

void get_memory_info(void) { // sysinfo would be more efficient, but it doesn't relay the available memory information (si_mem_available()), so the agent reads from procfs files (and only using sysinfo for the uptime seems not efficient either)
    OPEN_READ("/proc/meminfo", goto fail;, 64)
    char *ptr = file_buf + strlen("MemTotal:");
    while (isspace(*++ptr) && IN_READ(ptr));
    if (!IN_READ(ptr))
        goto fail;
    details.ram_size = (uint64)strtoull(ptr, NULL, 10) * PROC_MEMINFO_KB;
    while (*++ptr != 'F' && IN_READ(ptr)); // Free
    if (!IN_READ(ptr))
        goto fail;
    while (*++ptr != 'A' && IN_READ(ptr)); // Available
    if (!IN_READ(ptr))
        goto fail;
    ptr += strlen("vailable:");
    while (isspace(*++ptr) && IN_READ(ptr));
    if (!IN_READ(ptr))
        goto fail;
    if (details.ram_size)
        DOUBLE_TO_TWO_UINT8(current_stats->ram_usage, 100.0 * ((double)(details.ram_size - ((uint64)strtoull(ptr, NULL, 10) * PROC_MEMINFO_KB)) / details.ram_size))
    else
        TWO_UINT8_ZERO(current_stats->ram_usage);
    if ((ptr = strstr(file_buf, "SwapTotal:"))) {
        ptr += strlen("SwapTotal:");
        while (isspace(*ptr) && IN_READ(ptr))
            ++ptr;
        if (!IN_READ(ptr))
            goto fail;
        details.swap_size = (uint64)strtoull(ptr, NULL, 10) * PROC_MEMINFO_KB;
    } else 
        details.swap_size = 0;
    if (details.swap_size && (ptr = strstr(file_buf, "SwapFree:"))) {
        ptr += strlen("SwapFree:");
        while (isspace(*ptr) && IN_READ(ptr))
            ++ptr;
        if (!IN_READ(ptr))
            goto fail;
        DOUBLE_TO_TWO_UINT8(current_stats->swap_usage, 100.0 * ((double)(details.swap_size - ((uint64)strtoull(ptr, NULL, 10) * PROC_MEMINFO_KB)) / details.swap_size))
    } else
        TWO_UINT8_ZERO(current_stats->swap_usage);
    return;
fail:
    details.ram_size = details.swap_size = 0;
    TWO_UINT8_ZERO(current_stats->ram_usage);
    TWO_UINT8_ZERO(current_stats->swap_usage);
}

void get_disk_info(void) {
    char **ptr = mount_paths;
    details.disk_size = 0;
    uint64 disk_free = 0;
    struct statvfs stat;
    while (*ptr) {
        if (!statvfs(*ptr, &stat)) {
            details.disk_size += (uint64)stat.f_blocks * stat.f_frsize;
            disk_free += (uint64)stat.f_bfree * stat.f_frsize;
        }
        ++ptr;
    }
    if (details.disk_size)
        DOUBLE_TO_TWO_UINT8(current_stats->disk_usage, 100.0 * ((double)(details.disk_size - disk_free) / (double)details.disk_size))
    else
        TWO_UINT8_ZERO(current_stats->disk_usage);
}

bool sock_ready(int fd, bool read) {
    struct pollfd pfd = { .fd = fd, .events = read ? POLLIN : POLLOUT, .revents = 0 };
    if (poll(&pfd, 1, 2500) > 0)
        return (pfd.revents & (read ? POLLIN : POLLOUT)) && !(pfd.revents & (POLLERR | POLLHUP | POLLNVAL));
    return false;
}

int sock_read(void *ctx, uint8 *buf, size_t len) {
    if (!sock_ready(*(int *)ctx, true))
        return -1;
    int r = read(*(int *)ctx, buf, len);
    return r ? r : -1;
}
int sock_write(void *ctx, const uint8 *buf, size_t len) {
    if (!sock_ready(*(int *)ctx, false))
        return -1;
    int w = write(*(int *)ctx, buf, len);
    return w ? w : -1;
}

int setup_bearssl_connection(int *fd) {
    struct addrinfo hints, *addrinfo, *cur;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(host, "443", &hints, &addrinfo))
        return -2;
    for (cur = addrinfo; cur; cur = cur->ai_next) {
        *fd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (*fd < 0)
            continue;
        int opt = 3;
        if (setsockopt(*fd, IPPROTO_TCP, TCP_SYNCNT, &opt, sizeof(opt))) {
            close(*fd);
            continue;
        }
        if (!connect(*fd, cur->ai_addr, cur->ai_addrlen)) {
            unsigned int timeout = 5000;
            struct timeval timeout_struct = { .tv_sec = 5, .tv_usec = 0 };
            if (setsockopt(*fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout, sizeof(timeout)) ||
                setsockopt(*fd, SOL_SOCKET, SO_RCVTIMEO, &timeout_struct, sizeof(timeout_struct)) ||
                setsockopt(*fd, SOL_SOCKET, SO_SNDTIMEO, &timeout_struct, sizeof(timeout_struct))) {
                close(*fd);
                continue;
            }
            break;
        }
        close(*fd);
    }
    freeaddrinfo(addrinfo);
    if (!cur)
        return -3;
    br_ssl_client_reset(&client_context, host, 1);
    br_sslio_init(&sslio_context, &client_context.eng, sock_read, fd, sock_write, fd);
    return 0;
}

int send_https_request(void) {
    int fd, err = setup_bearssl_connection(&fd), ret = -6;
    int16 len;
    if (err)
        return err;
    if (br_sslio_write_all(&sslio_context, http_buf, http_req_len)) {
        br_sslio_close(&sslio_context);
        close(fd);
        return -4;
    }
    if (br_sslio_flush(&sslio_context)) {
        br_sslio_close(&sslio_context);
        close(fd);
        return -5;
    }
    if ((len = br_sslio_read(&sslio_context, &response_buf, sizeof(response_buf))) >= (int16)strlen("HTTP/1.1 200\r\n\r\n"))
        for (uint16 i = strlen("HTTP/1.1 200\r\n\r\n"); i < len; ++i)
            if (response_buf[i] == '\n' && response_buf[i - 2] == '\n' && response_buf[i - 1] == '\r' && response_buf[i - 3] == '\r') {
                if (len >= i + 1 && response_buf[i + 1] == '1')
                    ret = 0;
                break;
            }
    br_sslio_close(&sslio_context);
    close(fd);
    return ret;
}

bool collect(void) {
    ++stats_pos;
    if (stats_count < CONFIG_MAX_CACHED)
        ++stats_count;
    if (stats_pos == CONFIG_MAX_CACHED)
        stats_pos = 0;
    current_stats = &stats[stats_pos];

    sleep(CONFIG_MEASURE_EVERY_N_SECONDS);
    get_current_jiffies(&cpu_end);
    get_network_stats(&net_rx_end, &net_tx_end);
    get_disk_stats(&disk_sectors_read_end, &disk_sectors_written_end);
    if (!cpu_start.total || !cpu_end.total || // if get_current_jiffies failed
        cpu_end.total <= cpu_start.total || cpu_end.iowait < cpu_start.iowait || cpu_end.steal < cpu_start.steal || cpu_end.work < cpu_start.work || // hibernate or similar (counter reset)
        !net_rx_start || !net_tx_start || !net_rx_end || !net_tx_end || // if get_network_stats failed
        net_rx_end < net_rx_start || net_tx_end < net_tx_start || // hibernate or similar (counter reset)
        !disk_sectors_read_end || !disk_sectors_written_end || !disk_sectors_read_start || !disk_sectors_written_start // if get_disk_stats failed
    ) {
        if (net_rx_end && net_tx_end // if the current get_network_stats was successful...
            && (!net_rx_start || !net_tx_start || net_rx_end < net_rx_start || net_tx_end < net_tx_start) // ...and the previous failed or woke from hibernation
        ) {
            net_rx_start = net_rx_end;
            net_tx_start = net_tx_end;
        }
        if (cpu_end.total // if the current get_current_jiffies was successful...
            && (cpu_end.total <= cpu_start.total || cpu_end.iowait < cpu_start.iowait || cpu_end.steal < cpu_start.steal || cpu_end.work < cpu_start.work || !cpu_start.total)) // ...and the previous failed or woke from hibernation
            memcpy(&cpu_start, &cpu_end, sizeof(jiffies_spent_t));
        if ((!disk_sectors_read_start || !disk_sectors_written_start) && disk_sectors_read_end && disk_sectors_written_end) { // if the previous get_disk_stats failed and the current was successful (the case of end < start isn't an error because of hot-swap disks)
            disk_sectors_read_start = disk_sectors_read_end;
            disk_sectors_written_start = disk_sectors_written_end;
        }
        return false; // woken from hibernation or an error occured while collecting stats
    }
    current_stats->time = (uint32)time(NULL) - (CONFIG_MEASURE_EVERY_N_SECONDS / 2);
    details.uptime = get_uptime();
    cpu_total_diff = (cpu_end.total - cpu_start.total);
    cpu_iowait_diff = (cpu_end.iowait - cpu_start.iowait);
    cpu_steal_diff = (cpu_end.steal - cpu_start.steal);
    cpu_work_diff = (cpu_end.work - cpu_start.work);
    DOUBLE_TO_TWO_UINT8(current_stats->cpu_usage, 100.0 * ((double)cpu_work_diff / (double)cpu_total_diff))
    DOUBLE_TO_TWO_UINT8(current_stats->cpu_iowait, 100.0 * ((double)cpu_iowait_diff / (double)cpu_total_diff))
    DOUBLE_TO_TWO_UINT8(current_stats->cpu_steal, 100.0 * ((double)cpu_steal_diff / (double)cpu_total_diff))
    get_memory_info();
    get_disk_info();
    current_stats->rx_bytes = net_rx_end - net_rx_start;
    current_stats->tx_bytes = net_tx_end - net_tx_start;
    net_rx_start = net_rx_end;
    net_tx_start = net_tx_end;
    current_stats->read_sectors = disk_sectors_read_end < disk_sectors_read_start ? 0 : disk_sectors_read_end - disk_sectors_read_start; // prevent integer underflow and allow for hibernating and hot-swap disks
    current_stats->written_sectors = disk_sectors_written_end < disk_sectors_written_start ? 0 : disk_sectors_written_end - disk_sectors_written_start;
    disk_sectors_read_start = disk_sectors_read_end;
    disk_sectors_written_start = disk_sectors_written_end;
    memcpy(&cpu_start, &cpu_end, sizeof(cpu_end));
    return true;
}

void upload(void) {
    uint32 content_length;
upload_data:
    if (stats_count > CONFIG_UPLOAD_MAX_N_STATS_AT_ONCE) {
        content_length = sizeof(net_header_t) + sizeof(stats_t) * CONFIG_UPLOAD_MAX_N_STATS_AT_ONCE;
        header.includes_details = 0;
        header.stats_count = CONFIG_UPLOAD_MAX_N_STATS_AT_ONCE;
    } else {
        content_length = sizeof(net_header_t) + sizeof(details_t) + sizeof(stats_t) * stats_count;
        header.includes_details = 1;
        header.stats_count = stats_count;
    }
    http_req_len = 0;
    str_append(http_buf, &http_req_len, "POST /submit HTTP/1.1\r\nHost: ");
    str_append(http_buf, &http_req_len, host);
    str_append(http_buf, &http_req_len, "\r\nContent-Length: ");
    str_append_uint(http_buf, &http_req_len, content_length);
    str_append(http_buf, &http_req_len, "\r\nConnection: close\r\n\r\n");
    str_append_len(http_buf, &http_req_len, (char *)&header, sizeof(header));
    if (header.includes_details)
        str_append_len(http_buf, &http_req_len, (char *)&details, sizeof(details));
    // Upload oldest first
    int32 pos = (int32)stats_pos - (int32)stats_count + 1;
    if (pos < 0)
        pos += CONFIG_MAX_CACHED;
    for (uint16 y = 0; y < header.stats_count; ++y, ++pos) {
        if (pos == CONFIG_MAX_CACHED)
            pos = 0;
        str_append_len(http_buf, &http_req_len, (char *)&stats[pos], sizeof(stats_t));
    }
    if (send_https_request()) {
        sleep(1);
        if (!send_https_request())
            goto success;
    } else {
success:
        if (stats_count -= header.stats_count)
            goto upload_data;
    }
}

void collect_and_upload(void) {
    if (collect())
        upload();
}

int main(int argc, char **argv) {
    COMPILE_TIME_CHECKS
    COMPILE_TIME_ASSERT(sizeof(jiffies_spent_t) == 96);
    if (argc < 2) {
        write(2, SLEN("Missing argument!\nmonitoring_agent DOMAIN [TOKEN_PATH] [MOUNT]...\n"));
        return 99;
    }
    host = argv[1];
    signal(SIGPIPE, SIG_IGN);
    int fd = open(argc >= 3 ? argv[2] : CONFIG_DEFAULT_TOKEN_PATH, O_RDONLY);
    if (fd == -1 || read(fd, header.token, 32) < 32) {
        close(fd);
        write(2, "Error reading token!\n", strlen("Error reading token!\n"));
        return 1;
    }
    close(fd);
    header.token[32] = '\0';
    header.version = 1;
    if (argc > 3) {
        argv[2] = "/";
        mount_paths = argv + 2;
    } else
        mount_paths = mount_paths_default;
    br_ssl_client_init_full(&client_context, &minimal_context, TAs, TAs_NUM);
    br_ssl_engine_set_buffer(&client_context.eng, iobuf, sizeof(iobuf), 1);
    details.cpu_cores = get_nprocs();
    copy_cpu_model();
    copy_linux_version();
    get_network_stats(&net_rx_start, &net_tx_start);
    get_disk_stats(&disk_sectors_read_start, &disk_sectors_written_start);
    get_current_jiffies(&cpu_start);
    for (;;)
        collect_and_upload();
    return 0;
}
