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

#include <endian.h>
#if __BYTE_ORDER != __LITTLE_ENDIAN
#error This program will not work on big endian architectures because there did not seem to be any need for that so far, as x86 always is and ARM and RISC-V usually are little endian.
#endif

#ifdef __dietlibc__
#define _GNU_SOURCE /* for u_intN_t */
#define __bitwise /**/
#endif

#include <sys/types.h>

typedef u_int8_t   uint8;
typedef u_int16_t  uint16;
typedef u_int32_t  uint32;
typedef u_int64_t  uint64;
typedef int8_t     int8;
typedef int16_t    int16;
typedef int32_t    int32;
typedef int64_t    int64;

inline void str_nullbyte(char *dest, uint16 *dest_len) {
    dest[*dest_len] = '\0';
}
typedef uint8      bool;

#define false 0
#define true  1

#define COMPILE_TIME_ASSERT(c) (void)sizeof(char[-!(c)?-!(c):1])
#define SLEN(s) s, strlen(s) // for constant strings, this usually can be optimized away

// content length = sizeof(net_header_t) + includes_details * sizeof(details_t) + stats_count * sizeof(stats_t)

#define PACKED __attribute__((__packed__))

typedef struct PACKED {
    char token[33];
    uint8 version : 7;
    bool includes_details : 1;
    uint8 stats_count;
} net_header_t;

typedef struct PACKED {
    uint32 uptime;
    char linux_version[32];
    char cpu_model[48];
    uint8 linux_version_len;
    uint8 cpu_model_len;
    uint16 cpu_cores;
    uint64 ram_size; // in B
    uint64 swap_size; // in B
    uint64 disk_size; // in B
} details_t;

typedef struct PACKED {
    uint32 time; // unix time, overflow will occur in 2106
    // more efficient to store it as two uint8's instead of double/float, could do 7-bit-bitfields but that possibly is slower because more data needs to be fetched because of alignment and would only yield minimal space savings
    uint8 cpu_usage_before_decimal;
    uint8 cpu_usage_after_decimal;
    uint8 cpu_iowait_before_decimal;
    uint8 cpu_iowait_after_decimal;
    uint8 cpu_steal_before_decimal;
    uint8 cpu_steal_after_decimal;
    uint8 ram_usage_before_decimal;
    uint8 ram_usage_after_decimal;
    uint8 swap_usage_before_decimal;
    uint8 swap_usage_after_decimal;
    uint8 disk_usage_before_decimal;
    uint8 disk_usage_after_decimal;
    uint64 rx_bytes : 48;
    uint64 tx_bytes : 48;
    uint64 read_sectors : 48;
    uint64 written_sectors : 48;
} stats_t;

#define COMPILE_TIME_CHECKS \
    COMPILE_TIME_ASSERT(sizeof(net_header_t) == 35); \
    COMPILE_TIME_ASSERT(sizeof(details_t) == 112); \
    COMPILE_TIME_ASSERT(sizeof(stats_t) == 40); \
    COMPILE_TIME_ASSERT(sizeof(uint8) == 1); \
    COMPILE_TIME_ASSERT(sizeof(int8) == 1); \
    COMPILE_TIME_ASSERT(sizeof(uint16) == 2); \
    COMPILE_TIME_ASSERT(sizeof(int16) == 2); \
    COMPILE_TIME_ASSERT(sizeof(uint32) == 4); \
    COMPILE_TIME_ASSERT(sizeof(int32) == 4); \
    COMPILE_TIME_ASSERT(sizeof(uint64) == 8); \
    COMPILE_TIME_ASSERT(sizeof(int64) == 8);
