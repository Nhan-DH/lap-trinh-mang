#ifndef SERVER_CRYPTO_H
#define SERVER_CRYPTO_H

/*
 * Module: server_crypto.h
 * Chức năng: Công khai prototype và kích thước output của module SHA-256.
 * Được gọi bởi: server_crypto.c và server_protocol.c.
 * Implementation nằm trong server_crypto.c.
 */
/* Kích thước output gồm 64 ký tự hex và ký tự kết thúc chuỗi '\0'. */
#define SHA256_HEX_SIZE 65

/**
 * @brief Tính SHA-256 thuần C theo chuẩn FIPS 180-2.
 *
 * Hàm nhận chuỗi đầu vào, thực hiện padding, message schedule và 64 vòng
 * nén SHA-256, sau đó chuyển digest 32 byte thành chuỗi hexadecimal lowercase.
 *
 * @param[in] text Chuỗi byte kết thúc bằng '\0' cần băm; không được là NULL.
 * @param[out] output Bộ đệm caller cấp có ít nhất SHA256_HEX_SIZE byte.
 *                    Khi thành công nhận 64 ký tự hex lowercase và '\0'.
 * @return int:
 *         - 1: Băm thành công và output hợp lệ.
 *         - 0: text hoặc output là NULL.
 */
int make_sha256(const char *text, char output[SHA256_HEX_SIZE]);

#endif
