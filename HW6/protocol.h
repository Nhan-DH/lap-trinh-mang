#ifndef HW6_PROTOCOL_H
#define HW6_PROTOCOL_H

#include <stddef.h>                 /* size_t dùng cho số byte truyền nhận. */
#include <stdint.h>                 /* uint64_t dùng cho kích thước tệp. */

#define CHUNK_SIZE 8192              /* Kích thước chunk truyền tệp: 8 KB. */
#define MAX_LINE_SIZE 4096           /* Kích thước dòng header tối đa. */
#define MAX_STRING_SIZE (1024U * 1024U) /* Giới hạn độ dài xâu nhận/gửi. */
#define MAX_FILENAME_SIZE 255        /* Độ dài tối đa tên tệp trong header. */
#define MAX_FILE_SIZE (10ULL * 1024ULL * 1024ULL) /* Tệp phải nhỏ hơn 10 MB. */

/**
 * @brief Gửi đủ một vùng dữ liệu qua TCP.
 *
 * TCP là byte stream nên một lần send() có thể chỉ gửi một phần.
 * Hàm lặp lại send() cho tới khi đủ dữ liệu hoặc phát hiện lỗi.
 *
 * @param[in] sockfd Socket TCP đã kết nối.
 * @param[in] buffer Vùng dữ liệu cần gửi.
 * @param[in] count Số byte cần gửi.
 * @return 0 nếu thành công, -1 nếu send() lỗi hoặc peer đóng kết nối.
 */
int send_all(int sockfd, const void *buffer, size_t count);

/**
 * @brief Nhận đủ một số byte xác định từ TCP.
 * @param[in] sockfd Socket TCP đã kết nối.
 * @param[out] buffer Vùng nhớ nhận dữ liệu.
 * @param[in] count Số byte bắt buộc phải nhận.
 * @return 0 nếu đủ dữ liệu, -1 nếu recv() lỗi hoặc peer đóng kết nối.
 */
int recv_all(int sockfd, void *buffer, size_t count);

/**
 * @brief Đọc một dòng header TCP tới ký tự '\n'.
 * @param[in] sockfd Socket TCP đã kết nối.
 * @param[out] line Bộ đệm nhận dòng, không chứa '\n' ở cuối.
 * @param[in] capacity Kích thước bộ đệm line.
 * @return Số ký tự đã nhận; -1 nếu lỗi, EOF hoặc header quá dài.
 */
int recv_line(int sockfd, char *line, size_t capacity);

/**
 * @brief Đổi số 64 bit giữa host byte order và network byte order.
 * POSIX không chuẩn hóa htonll/ntohll nên hàm ghép hai phần 32 bit.
 */
uint64_t host_to_network64(uint64_t value);
uint64_t network_to_host64(uint64_t value);

#endif
