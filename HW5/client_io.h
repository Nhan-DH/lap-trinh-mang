#ifndef CLIENT_IO_H
#define CLIENT_IO_H

/*
 * Module: client_io.h
 * Chức năng: Khai báo prototype và bộ đệm dùng chung cho client I/O.
 * Được gọi bởi: client.c.
 * Implementation nằm trong client_io.c.
 */
#include <netinet/in.h>      /* struct sockaddr_in dùng cho địa chỉ server */

#define MAX_TEXT 1024         /* Kích thước bộ đệm client */

/**
 * @brief Kiểm tra và chuyển chuỗi port thành số nguyên hợp lệ.
 * @param[in] text Chuỗi port cần phân tích.
 * @param[out] port Nơi nhận giá trị port sau khi chuyển đổi.
 * @return 1 nếu hợp lệ, 0 nếu sai định dạng hoặc ngoài khoảng 1..65535.
 */
int parse_port(const char *text, int *port);

/**
 * @brief Gửi một chuỗi đến server bằng UDP.
 * @param[in] socket_fd Socket UDP của client.
 * @param[in] message Chuỗi dữ liệu cần gửi.
 * @param[in] server Địa chỉ IP và port của server.
 * @return 1 nếu gửi thành công, 0 nếu sendto() thất bại.
 */
int send_message(int socket_fd, const char *message,
                 const struct sockaddr_in *server);

/**
 * @brief Gửi datagram rỗng để yêu cầu kết thúc phiên.
 * @param[in] socket_fd Socket UDP của client.
 * @param[in] server Địa chỉ IP và port của server.
 * @return 1 nếu gửi thành công, 0 nếu sendto() thất bại.
 */
int send_empty_message(int socket_fd, const struct sockaddr_in *server);

/**
 * @brief Nhận một phản hồi UDP từ server.
 * @param[in] socket_fd Socket UDP đang chờ dữ liệu.
 * @param[out] buffer Bộ đệm nhận phản hồi, luôn được kết thúc bằng '\0'.
 * @return 1 nếu nhận thành công, 0 nếu recvfrom() thất bại.
 */
int receive_message(int socket_fd, char buffer[MAX_TEXT]);

/**
 * @brief Đọc một dòng từ bàn phím và bỏ ký tự xuống dòng.
 * @param[out] buffer Bộ đệm nhận dữ liệu từ stdin.
 * @return 1 nếu đọc thành công, 0 nếu gặp EOF hoặc lỗi đọc.
 */
int read_line(char buffer[MAX_TEXT]);

#endif
