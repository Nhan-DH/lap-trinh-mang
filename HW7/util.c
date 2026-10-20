#define _GNU_SOURCE
#include "proto.h"
#include <errno.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

uint32_t crc32_calc(const void *data, size_t n, uint32_t crc)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    const uint8_t *p = data;
    crc = ~crc;
    while (n--)
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int chance(double percent)
{
    if (percent <= 0) return 0;
    return rand() / (RAND_MAX + 1.0) * 100.0 < percent;
}

/* ---- MD5 (OpenSSL EVP) ---- */
md5_t *md5_new(void)
{
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    EVP_DigestInit_ex(c, EVP_md5(), NULL);
    return (md5_t *)c;
}

void md5_update(md5_t *m, const void *data, size_t n)
{
    EVP_DigestUpdate((EVP_MD_CTX *)m, data, n);
}

void md5_final_hex(md5_t *m, char out[33])
{
    unsigned char d[EVP_MAX_MD_SIZE];
    unsigned int dl = 0;
    EVP_DigestFinal_ex((EVP_MD_CTX *)m, d, &dl);
    EVP_MD_CTX_free((EVP_MD_CTX *)m);
    for (unsigned i = 0; i < 16; i++)
        sprintf(out + 2 * i, "%02X", d[i]);
    out[32] = 0;
}

void md5_buf_hex(const void *data, size_t n, char out[33])
{
    md5_t *m = md5_new();
    md5_update(m, data, n);
    md5_final_hex(m, out);
}

int md5_fd_hex(int fd, char out[33])
{
    uint8_t buf[65536];
    off_t off = 0;
    md5_t *m = md5_new();
    for (;;) {
        ssize_t r = pread(fd, buf, sizeof buf, off);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            md5_final_hex(m, out);
            return -1;
        }
        if (r == 0) {
            break;
        }
        md5_update(m, buf, (size_t)r);
        off += r;
    }
    md5_final_hex(m, out);
    return 0;
}

void split_md5(const char md5[33], char *letters, char *digits)
{
    int l = 0, d = 0;
    for (int i = 0; md5[i]; i++) {
        if (md5[i] >= '0' && md5[i] <= '9') digits[d++] = md5[i];
        else letters[l++] = md5[i];
    }
    letters[l] = 0;
    digits[d] = 0;
}
