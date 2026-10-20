/*
 * util.c - Cac tien ich dung chung cua HW7:
 * CRC32, dong ho don dieu, mo phong loi va tinh MD5 bang OpenSSL EVP.
 */
#define _GNU_SOURCE
#include "proto.h"          /* Cac khai bao giao thuc va kieu MD5 */
#include <errno.h>          /* errno va EINTR khi doc file */
#include <openssl/evp.h>    /* API EVP dung de tinh MD5 */
#include <stdio.h>          /* sprintf tao chuoi MD5 dang hex */
#include <stdlib.h>         /* rand va RAND_MAX cho mo phong loi */
#include <time.h>           /* clock_gettime va CLOCK_MONOTONIC */
#include <unistd.h>         /* pread doc file theo offset */

/**
 * @brief Tinh CRC32 cho mot vung du lieu.
 * @param[in] data Vung du lieu can tinh CRC.
 * @param[in] n So byte cua vung du lieu.
 * @param[in] crc Gia tri CRC truoc do, cho phep tinh noi tiep.
 * @return Gia tri CRC32 sau khi xu ly vung du lieu.
 */
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

/**
 * @brief Lay thoi gian don dieu tinh bang mili-giay.
 * @return So mili-giay tu mot moc don dieu cua he thong.
 */
long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/**
 * @brief Tra ve true theo mot xac suat de mo phong loi mang.
 * @param[in] percent Xac suat trong khoang 0..100.
 * @return 1 neu su kien xay ra, 0 neu khong.
 */
int chance(double percent)
{
    if (percent <= 0) return 0;
    return rand() / (RAND_MAX + 1.0) * 100.0 < percent;
}

/* ---- MD5 streaming wrapper (OpenSSL EVP) ---- */
/**
 * @brief Tao bo dem MD5 moi.
 * @return Con tro context MD5, hoac NULL neu OpenSSL khong cap phat duoc.
 */
md5_t *md5_new(void)
{
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    EVP_DigestInit_ex(c, EVP_md5(), NULL);
    return (md5_t *)c;
}

/**
 * @brief Nap them n byte vao bo dem MD5.
 * @param[in,out] m Context MD5 dang xu ly.
 * @param[in] data Vung du lieu can nap.
 * @param[in] n So byte can nap.
 */
void md5_update(md5_t *m, const void *data, size_t n)
{
    EVP_DigestUpdate((EVP_MD_CTX *)m, data, n);
}

/**
 * @brief Ket thuc MD5, ghi 32 ky tu hex in hoa va giai phong context.
 * @param[in,out] m Context MD5; ham se giai phong context nay.
 * @param[out] out Bo dem phai co it nhat 33 byte.
 */
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

/**
 * @brief Tinh MD5 cho mot vung nho va tra ve dang hex in hoa.
 * @param[in] data Vung du lieu dau vao.
 * @param[in] n So byte dau vao.
 * @param[out] out Bo dem phai co it nhat 33 byte.
 */
void md5_buf_hex(const void *data, size_t n, char out[33])
{
    md5_t *m = md5_new();
    md5_update(m, data, n);
    md5_final_hex(m, out);
}

/**
 * @brief Tinh MD5 file theo tung block, khong nap ca file vao RAM.
 * @param[in] fd File descriptor da mo de doc.
 * @param[out] out Bo dem phai co it nhat 33 byte.
 * @return 0 neu thanh cong, -1 neu pread() gap loi.
 */
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

/**
 * @brief Tach chuoi MD5 hex thanh hai chuoi gom chu cai va chu so.
 * @param[in] md5 Chuoi MD5 32 ky tu in hoa.
 * @param[out] letters Noi nhan cac ky tu A-F.
 * @param[out] digits Noi nhan cac ky tu 0-9.
 */
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
