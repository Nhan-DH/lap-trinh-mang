/*
 * crypto.c - AES-256-CBC và SHA-256 dùng OpenSSL (ĐÃ CUNG CẤP, không cần sửa)
 *
 * Yêu cầu hệ thống: libssl-dev (Ubuntu/Debian: sudo apt install libssl-dev)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "common.h"

/* Khóa đối xứng DÙNG CHUNG cho server và mọi client (đúng 32 byte).
 * Đây là khóa cố định của bài tập; trong thực tế khóa phải được trao đổi an toàn. */
static const unsigned char SHARED_KEY[] = "IT4062_HW8_AES256_KEY_DEMO_2026!";
_Static_assert(sizeof(SHARED_KEY) - 1 == AES_KEY_LEN, "khoa phai dung 32 byte");

unsigned char *crypto_encrypt(const unsigned char *in, size_t inlen, size_t *outlen)
{
    unsigned char iv[IV_LEN];
    unsigned char *out = NULL;
    EVP_CIPHER_CTX *ctx = NULL;
    int l1 = 0, l2 = 0;

    if (RAND_bytes(iv, IV_LEN) != 1)
        return NULL;
    out = malloc(IV_LEN + inlen + 16);      /* + 16: khối padding tối đa */
    if (!out)
        return NULL;

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        goto fail;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, SHARED_KEY, iv) != 1)
        goto fail;
    if (EVP_EncryptUpdate(ctx, out + IV_LEN, &l1, in, (int)inlen) != 1)
        goto fail;
    if (EVP_EncryptFinal_ex(ctx, out + IV_LEN + l1, &l2) != 1)
        goto fail;

    memcpy(out, iv, IV_LEN);
    *outlen = IV_LEN + (size_t)l1 + (size_t)l2;
    EVP_CIPHER_CTX_free(ctx);
    return out;

fail:
    ERR_clear_error();
    EVP_CIPHER_CTX_free(ctx);
    free(out);
    return NULL;
}

unsigned char *crypto_decrypt(const unsigned char *in, size_t inlen, size_t *outlen)
{
    unsigned char *out = NULL;
    EVP_CIPHER_CTX *ctx = NULL;
    size_t ctlen;
    int l1 = 0, l2 = 0;

    /* Hợp lệ: có IV, ít nhất 1 khối ciphertext, và độ dài là bội của 16 */
    if (inlen < IV_LEN + 16 || (inlen - IV_LEN) % 16 != 0)
        return NULL;
    ctlen = inlen - IV_LEN;
    out = malloc(ctlen);
    if (!out)
        return NULL;

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        goto fail;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, SHARED_KEY, in) != 1)
        goto fail;
    if (EVP_DecryptUpdate(ctx, out, &l1, in + IV_LEN, (int)ctlen) != 1)
        goto fail;
    if (EVP_DecryptFinal_ex(ctx, out + l1, &l2) != 1)   /* padding sai */
        goto fail;

    *outlen = (size_t)l1 + (size_t)l2;
    EVP_CIPHER_CTX_free(ctx);
    return out;

fail:
    ERR_clear_error();
    EVP_CIPHER_CTX_free(ctx);
    free(out);
    return NULL;
}

int sha256_hex(const unsigned char *data, size_t len, char hex[65])
{
    unsigned char md[SHA256_DIGEST_LENGTH];
    int i;

    if (!SHA256(data, len, md))
        return -1;
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
        snprintf(hex + 2 * i, 3, "%02x", md[i]);
    hex[64] = '\0';
    return 0;
}
