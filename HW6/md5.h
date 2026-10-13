#ifndef HW6_MD5_H
#define HW6_MD5_H

#include <stddef.h>                 /* size_t là kiểu độ dài dữ liệu. */
#include <stdio.h>                  /* FILE dùng để lưu dữ liệu tạm thời. */

#define MD5_DIGEST_LENGTH 16        /* MD5 tạo ra 128 bit = 16 byte. */

typedef struct {
    FILE *temporary_file;            /* Tệp tạm chứa dữ liệu cần tính MD5. */
    char temporary_path[64];         /* Đường dẫn tệp tạm do mkstemp tạo. */
} MD5_CTX;

/**
 * @brief Khởi tạo một phiên tính MD5 dựa trên lệnh md5sum của Linux.
 * @param[out] context Trạng thái phiên MD5 cần khởi tạo.
 */
void md5_init(MD5_CTX *context);

/**
 * @brief Ghi thêm dữ liệu vào tệp tạm của phiên MD5.
 * @param[in,out] context Trạng thái phiên MD5.
 * @param[in] data Vùng dữ liệu cần ghi.
 * @param[in] length Số byte cần ghi.
 */
void md5_update(MD5_CTX *context, const void *data, size_t length);

/**
 * @brief Đóng tệp tạm, gọi md5sum và lấy digest nhị phân 16 byte.
 * @param[out] digest Vùng nhận kết quả MD5.
 * @param[in,out] context Trạng thái phiên cần hoàn tất.
 */
void md5_final(unsigned char digest[MD5_DIGEST_LENGTH], MD5_CTX *context);

/**
 * @brief Đổi digest nhị phân thành chuỗi MD5 gồm 32 chữ in hoa.
 * @param[in] digest Digest 16 byte.
 * @param[out] output Bộ đệm phải có ít nhất 33 byte.
 */
void md5_hex(const unsigned char digest[MD5_DIGEST_LENGTH],
             char output[MD5_DIGEST_LENGTH * 2 + 1]);

/**
 * @brief Tính MD5 cho một xâu và trả về dạng hex in hoa.
 * @param[in] text Xâu đầu vào kết thúc bằng '\0'.
 * @param[out] output Bộ đệm phải có ít nhất 33 byte.
 */
void md5_string(const char *text, char output[MD5_DIGEST_LENGTH * 2 + 1]);

#endif
