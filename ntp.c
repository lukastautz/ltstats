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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#define NTP_SERVER "pool.ntp.org"
#define UNIX_OFFSET 2208988800L // see RFC 868
#define NTP_FLAGS 35 // 0b00100011 LI=0 (00) VN=4 (100) MODE=3 (011)

typedef struct __attribute__((__packed__)) {
    uint8_t flags;
    uint8_t not_needed[39];
    uint32_t ts_secs;
    uint32_t ts_frac;
} ntp_packet;

void ntp() {
    int sock = -1, status;
    struct addrinfo hints, *addrinfo, *cur;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_protocol = IPPROTO_UDP;;
    ntp_packet packet = { .flags = NTP_FLAGS };
    struct timeval time = { .tv_sec = 1, .tv_usec = 0 };
    if ((status = getaddrinfo(NTP_SERVER, "123", &hints, &addrinfo)) != 0)
        return;
    for (cur = addrinfo; cur; cur = cur->ai_next) {
        sock = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sock != -1)
            break;
    }
    if (sock == -1) {
        freeaddrinfo(addrinfo);
        return;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time)) < 0 ||
        sendto(sock, &packet, sizeof(packet), 0, cur->ai_addr, cur->ai_addrlen) != sizeof(packet) ||
        recvfrom(sock, &packet, sizeof(packet), 0, NULL, NULL) != sizeof(packet)) {
        freeaddrinfo(addrinfo);
        close(sock);
        return;
    }
    freeaddrinfo(addrinfo);
    close(sock);
    packet.ts_secs = ntohl(packet.ts_secs);
    packet.ts_frac = ntohl(packet.ts_frac);
    time.tv_sec = packet.ts_secs - UNIX_OFFSET;
    time.tv_usec = (uint32_t)((double)packet.ts_frac * 1.0e6 / (double)(1LL << 32)); 
    settimeofday(&time, NULL);
}

int main() {
    for (;;) {
        ntp();
        sleep(300);
    }
}
