/*
 * common.h - Khai báo dùng chung cho server và client (IT4062 - HW8)
 *
 * KHÔNG sửa các hằng số giao thức và chữ ký hàm ở file này: bộ tự chấm
 * dựa vào đúng các giá trị này.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>

/* ---- Giới hạn của giao thức ---- */
#define MIN_USER_LEN     3
#define MAX_USER_LEN    20
#define MIN_PASS_LEN     3
#define MAX_PASS_LEN    32
#define MAX_TEXT_LEN  1024                       /* byte: nội dung MSG      */
#define MAX_FNAME_LEN  128                       /* byte: tên file          */
#define MAX_FILE_SIZE  (10u * 1024u * 1024u)     /* 10 MiB                  */
#define MAX_HEADER_LEN 2048                      /* byte: dòng lệnh         */
#define MAX_FRAME_LEN  (12u * 1024u * 1024u)     /* byte: LEN tối đa        */
#define IV_LEN           16
#define AES_KEY_LEN      32
#define ACCOUNT_FILE   "account.txt"
#define DOWNLOAD_DIR   "downloads"

/* ---- Mã lỗi trong dòng "ERR <mã> <thông điệp>" ---- */
#define ERR_BAD_REQUEST  400  /* sai cú pháp / tham số / định dạng          */
#define ERR_BAD_CRED     401  /* LOGIN sai tài khoản hoặc mật khẩu           */
#define ERR_FORBIDDEN    403  /* cần đăng nhập, hoặc REGISTER/LOGIN khi đã đăng nhập */
#define ERR_NOT_FOUND    404  /* người nhận không online                     */
#define ERR_CONFLICT     409  /* REGISTER trùng user; LOGIN user đang online */
#define ERR_TOO_LARGE    413  /* file hoặc frame quá lớn                     */
#define ERR_INTERNAL     500  /* lỗi hệ thống                                */

/* ---- Giá trị trả về của recv_msg() ---- */
#define RECV_OK        0
#define RECV_IO       -1   /* lỗi socket, hoặc đối phương đã đóng kết nối    */
#define RECV_TOO_BIG  -2   /* LEN > MAX_FRAME_LEN (chưa đọc phần thân)       */
#define RECV_BAD      -3   /* đã đọc đủ frame nhưng không giải mã được      */

/* ---- crypto.c (ĐÃ CUNG CẤP) ---- */
/* Mã hóa AES-256-CBC với IV ngẫu nhiên. Trả về buffer mới dạng IV||CIPHERTEXT
 * (caller tự free), hoặc NULL nếu lỗi. */
unsigned char *crypto_encrypt(const unsigned char *in, size_t inlen, size_t *outlen);
/* Giải mã buffer dạng IV||CIPHERTEXT. Trả về buffer mới (caller tự free),
 * hoặc NULL nếu độ dài sai hoặc padding sai. */
unsigned char *crypto_decrypt(const unsigned char *in, size_t inlen, size_t *outlen);
/* SHA-256 của data, ghi hex chữ thường (64 ký tự + '\0') vào hex[65]. */
int sha256_hex(const unsigned char *data, size_t len, char hex[65]);

/* ---- net.c (ĐÃ CUNG CẤP) ---- */
/* Gửi/nhận đủ len byte. Trả 0 nếu thành công, -1 nếu lỗi hoặc EOF. */
int send_all(int fd, const void *buf, size_t len);
int recv_all(int fd, void *buf, size_t len);

/* Đóng gói plaintext = header + '\n' + body, mã hóa, rồi gửi một frame.
 * KHÔNG an toàn đa luồng: phải khóa mutex của socket trước khi gọi. */
int send_msg(int fd, const char *header, const void *body, size_t blen);

/* Nhận một frame, giải mã. Kết quả plaintext được cấp phát mới (caller free).
 * Trả RECV_OK / RECV_IO / RECV_TOO_BIG / RECV_BAD. */
int recv_msg(int fd, unsigned char **plain, size_t *plen);

/* Tách plaintext thành header (đến '\n' đầu tiên, có '\0' kết thúc) và body.
 * Trả 0 nếu thành công, -1 nếu không có '\n' hoặc header quá dài (>= hcap). */
int split_msg(const unsigned char *plain, size_t plen,
              char *header, size_t hcap,
              const unsigned char **body, size_t *blen);

#endif /* COMMON_H */
