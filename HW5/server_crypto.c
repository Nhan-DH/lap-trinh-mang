/*
 * Module: server_crypto.c
 * Chức năng: Cài đặt SHA-256 thuần C theo chuẩn FIPS 180-2.
 * Được gọi bởi: server_protocol.c gọi make_sha256() sau khi kiểm tra chuỗi.
 * Gọi tới: chỉ dùng thư viện C chuẩn; không phụ thuộc OpenSSL hoặc libcrypto.
 */
#include <stdint.h>         /* Kiểu uint32_t và uint64_t của SHA-256 */
#include <stdio.h>          /* snprintf chuyển digest sang chuỗi hex */
#include <string.h>         /* strlen, memcpy và memset xử lý dữ liệu */
#include "server_crypto.h"  /* Prototype và kích thước output dùng chung */

#define SHA256_BLOCK_SIZE 64  /* Kích thước một block SHA-256 tính theo byte */
#define SHA256_DIGEST_SIZE 32 /* Kích thước digest SHA-256 tính theo byte */

typedef struct {
    uint32_t state[8];                    /* Tám thanh ghi trạng thái SHA-256 */
    uint64_t bit_count;                   /* Số bit dữ liệu đã nhận */
    unsigned char buffer[SHA256_BLOCK_SIZE]; /* Block chưa đủ 64 byte */
    size_t buffer_length;                 /* Số byte hiện có trong buffer */
} Sha256Context;

static const uint32_t round_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

/**
 * @brief Xoay phải một số nguyên 32-bit.
 *
 * @param[in] value Giá trị cần xoay.
 * @param[in] amount Số bit cần xoay sang phải.
 * @return Giá trị sau khi xoay phải.
 */
static uint32_t rotate_right(uint32_t value, unsigned int amount)
{
    return (value >> amount) | (value << (32U - amount));
}

/**
 * @brief Nén một block 512-bit vào trạng thái SHA-256.
 *
 * Block được đọc theo big-endian và xử lý qua message schedule cùng 64 vòng
 * nén được quy định trong FIPS 180-2.
 *
 * @param[in,out] context Ngữ cảnh SHA-256 cần cập nhật trạng thái.
 * @param[in] block Block dữ liệu đủ 64 byte cần nén.
 */
static void transform_block(Sha256Context *context,
                            const unsigned char block[SHA256_BLOCK_SIZE])
{
    uint32_t words[64]; /* Message schedule gồm 64 từ 32-bit */
    uint32_t working[8]; /* Trạng thái tạm trong 64 vòng nén */
    unsigned int index;   /* Chỉ số word hoặc thanh ghi trạng thái */

    for (index = 0; index < 16U; index++) {
        words[index] = ((uint32_t)block[index * 4U] << 24U) |
                       ((uint32_t)block[index * 4U + 1U] << 16U) |
                       ((uint32_t)block[index * 4U + 2U] << 8U) |
                       (uint32_t)block[index * 4U + 3U];
    }
    for (index = 16; index < 64U; index++) {
        uint32_t sigma0 = rotate_right(words[index - 15U], 7U) ^
                          rotate_right(words[index - 15U], 18U) ^
                          (words[index - 15U] >> 3U);
        uint32_t sigma1 = rotate_right(words[index - 2U], 17U) ^
                          rotate_right(words[index - 2U], 19U) ^
                          (words[index - 2U] >> 10U);

        words[index] = words[index - 16U] + sigma0 +
                       words[index - 7U] + sigma1;
    }
    for (index = 0; index < 8U; index++) {
        working[index] = context->state[index];
    }
    for (index = 0; index < 64U; index++) {
        uint32_t big_sigma0 = rotate_right(working[0], 2U) ^
                              rotate_right(working[0], 13U) ^
                              rotate_right(working[0], 22U);
        uint32_t big_sigma1 = rotate_right(working[4], 6U) ^
                              rotate_right(working[4], 11U) ^
                              rotate_right(working[4], 25U);
        uint32_t choice = (working[4] & working[5]) ^
                          ((~working[4]) & working[6]);
        uint32_t majority = (working[0] & working[1]) ^
                            (working[0] & working[2]) ^
                            (working[1] & working[2]);
        uint32_t temp1 = working[7] + big_sigma1 + choice +
                         round_constants[index] + words[index];
        uint32_t temp2 = big_sigma0 + majority;

        working[7] = working[6];
        working[6] = working[5];
        working[5] = working[4];
        working[4] = working[3] + temp1;
        working[3] = working[2];
        working[2] = working[1];
        working[1] = working[0];
        working[0] = temp1 + temp2;
    }
    for (index = 0; index < 8U; index++) {
        context->state[index] += working[index];
    }
}

/**
 * @brief Khởi tạo trạng thái ban đầu của SHA-256.
 *
 * @param[out] context Ngữ cảnh nhận các hằng số khởi tạo FIPS 180-2.
 */
static void initialize_context(Sha256Context *context)
{
    context->state[0] = 0x6a09e667U;
    context->state[1] = 0xbb67ae85U;
    context->state[2] = 0x3c6ef372U;
    context->state[3] = 0xa54ff53aU;
    context->state[4] = 0x510e527fU;
    context->state[5] = 0x9b05688cU;
    context->state[6] = 0x1f83d9abU;
    context->state[7] = 0x5be0cd19U;
    context->bit_count = 0;
    context->buffer_length = 0;
}

/**
 * @brief Nạp dữ liệu đầu vào vào context SHA-256.
 *
 * @param[in,out] context Ngữ cảnh cần cập nhật.
 * @param[in] data Dữ liệu đầu vào.
 * @param[in] length Số byte dữ liệu đầu vào.
 */
static void update_context(Sha256Context *context,
                           const unsigned char *data, size_t length)
{
    size_t copied; /* Số byte được chép vào block hiện tại */

    context->bit_count += (uint64_t)length * 8U;
    while (length > 0) {
        copied = SHA256_BLOCK_SIZE - context->buffer_length;
        if (copied > length) {
            copied = length;
        }
        memcpy(context->buffer + context->buffer_length, data, copied);
        context->buffer_length += copied;
        data += copied;
        length -= copied;
        if (context->buffer_length == SHA256_BLOCK_SIZE) {
            transform_block(context, context->buffer);
            context->buffer_length = 0;
        }
    }
}

/**
 * @brief Hoàn tất padding và lấy digest SHA-256 dạng byte.
 *
 * @param[in,out] context Ngữ cảnh đã nhận toàn bộ dữ liệu đầu vào.
 * @param[out] digest Bộ đệm nhận 32 byte digest.
 */
static void finalize_context(Sha256Context *context,
                             unsigned char digest[SHA256_DIGEST_SIZE])
{
    unsigned int index; /* Chỉ số byte trong digest hoặc độ dài bit */

    context->buffer[context->buffer_length++] = 0x80U;
    while (context->buffer_length != 56U) {
        if (context->buffer_length == SHA256_BLOCK_SIZE) {
            transform_block(context, context->buffer);
            context->buffer_length = 0;
        }
        context->buffer[context->buffer_length++] = 0;
    }
    for (index = 0; index < 8U; index++) {
        context->buffer[56U + index] =
            (unsigned char)(context->bit_count >> (56U - index * 8U));
    }
    transform_block(context, context->buffer);
    for (index = 0; index < 8U; index++) {
        digest[index * 4U] = (unsigned char)(context->state[index] >> 24U);
        digest[index * 4U + 1U] = (unsigned char)(context->state[index] >> 16U);
        digest[index * 4U + 2U] = (unsigned char)(context->state[index] >> 8U);
        digest[index * 4U + 3U] = (unsigned char)context->state[index];
    }
}

/**
 * @brief Tính SHA-256 thuần C theo chuẩn FIPS 180-2.
 *
 * @param[in] text Chuỗi chữ và số cần băm.
 * @param[out] output Bộ đệm nhận 64 ký tự hex lowercase và '\0'.
 * @return 1 nếu băm thành công, 0 nếu tham số đầu vào là NULL.
 */
int make_sha256(const char *text, char output[SHA256_HEX_SIZE])
{
    Sha256Context context; /* Ngữ cảnh xử lý SHA-256 */
    unsigned char digest[SHA256_DIGEST_SIZE]; /* Digest dạng byte */
    unsigned int index; /* Vị trí byte digest đang chuyển sang hex */

    if (text == NULL || output == NULL) {
        return 0;
    }
    initialize_context(&context);
    update_context(&context, (const unsigned char *)text, strlen(text));
    finalize_context(&context, digest);
    for (index = 0; index < SHA256_DIGEST_SIZE; index++) {
        (void)snprintf(output + index * 2U, 3, "%02x", digest[index]);
    }
    output[SHA256_HEX_SIZE - 1U] = '\0';
    return 1;
}
