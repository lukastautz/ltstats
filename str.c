#define str_append(d, l, s) str_append_len(d, l, s, strlen(s))

void str_append_len(char *dest, uint16 *dest_len, const char *src, uint16 len) {
    memcpy(dest + *dest_len, src, len);
    *dest_len += len;
}

uint8 itoa(uint32 n, char *s) {
    uint8 i = 0, y = 0, z;
    do
        s[i] = n % 10 + '0', ++i;
    while ((n /= 10) > 0);
    z = i - 1;
    for (char c; y < z; ++y, --z)
        c = s[y], s[y] = s[z], s[z] = c;
    return i;
}

uint8 itoa_fill(uint32 n, char *dest, uint8 fill_to) {
    uint8 i = 0, z = 0, j;
    char tmp;
    while (n) {
        dest[i++] = (n % 10) + '0';
        n /= 10;
    }
    while (i < fill_to)
        dest[i++] = '0';
    j = i - 1;
    while (z < j) {
        tmp = dest[z], dest[z] = dest[j], dest[j] = tmp;
        ++z, --j;
    }
    return i;
}

#define str_append_uint(dest, dest_len, value) *dest_len += itoa(value, dest + *dest_len);

void str_append_percent(char *dest, uint16 *dest_len, double value) {
    uint8 tmp = value;
    *dest_len += itoa(value, dest + *dest_len);
    dest[(*dest_len)++] = '.';
    tmp = 100 * (value - (double)tmp);
    *dest_len += itoa_fill(tmp, dest + *dest_len, 2);
}