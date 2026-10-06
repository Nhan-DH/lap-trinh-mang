/*
 * Module: client_io.c
 * Chức năng: Cung cấp hàm I/O dùng chung cho client UDP.
 * Được gọi bởi: client.c gọi parse_port(), read_line(), send_message(),
 *               send_empty_message() và receive_message().
 * Gọi tới: stdin để đọc bàn phím và socket API sendto()/recvfrom().
 */
#include <stdio.h>          /* fgets đọc bàn phím */
#include <stdlib.h>         /* strtol dùng kiểm tra port */
#include <string.h>         /* strlen, strcspn xử lý chuỗi */
#include <errno.h>          /* errno kiểm tra lỗi strtol */
#include <sys/socket.h>     /* sendto, recvfrom giao tiếp UDP */
#include "client_io.h"      /* Prototype và MAX_TEXT dùng chung */

/**
 * @brief Chuyển chuỗi port thành số và kiểm tra phạm vi hợp lệ.
 *
 * @param[in] text Chuỗi port nhận từ dòng lệnh.
 * @param[out] port Nơi lưu port sau khi chuyển đổi.
 * @return 1 nếu port thuộc khoảng 1..65535, 0 nếu không hợp lệ.
 */
int parse_port(const char *text, int *port)
{
    char *end_pointer; /* Vị trí kết thúc phần số */
    long value;        /* Giá trị port tạm thời */

    errno = 0;
    value = strtol(text, &end_pointer, 10);
    if (errno != 0 || text[0] == '\0' || *end_pointer != '\0' ||
        value < 1 || value > 65535) {
        return 0;
    }
    *port = (int)value;
    return 1;
}

/**
 * @brief Gửi chuỗi đến server qua UDP bằng sendto().
 *
 * @param[in] socket_fd Socket UDP của client.
 * @param[in] message Dữ liệu cần gửi, không bao gồm byte '\0'.
 * @param[in] server Địa chỉ IPv4 và port của server.
 * @return 1 nếu gửi thành công, 0 nếu sendto() trả về lỗi.
 */
int send_message(int socket_fd, const char *message,
                 const struct sockaddr_in *server)
{
    size_t length; /* Số byte cần gửi */
    ssize_t sent;  /* Số byte sendto() đã gửi */

    length = strlen(message);
    sent = sendto(socket_fd, message, length, 0,
                  (const struct sockaddr *)server, sizeof(*server));
    return sent >= 0;
}

/**
 * @brief Gửi datagram UDP rỗng để yêu cầu logout.
 *
 * @param[in] socket_fd Socket UDP của client.
 * @param[in] server Địa chỉ IPv4 và port của server.
 * @return 1 nếu gửi thành công, 0 nếu sendto() trả về lỗi.
 */
int send_empty_message(int socket_fd, const struct sockaddr_in *server)
{
    ssize_t sent; /* Kết quả trả về của sendto() */

    sent = sendto(socket_fd, "", 0, 0,
                  (const struct sockaddr *)server, sizeof(*server));
    return sent >= 0;
}

/**
 * @brief Nhận phản hồi UDP từ server bằng recvfrom().
 *
 * @param[in] socket_fd Socket UDP đang chờ phản hồi.
 * @param[out] buffer Bộ đệm nhận dữ liệu, chừa một byte cho '\0'.
 * @return 1 nếu nhận thành công, 0 nếu recvfrom() trả về lỗi.
 */
int receive_message(int socket_fd, char buffer[MAX_TEXT])
{
    ssize_t received; /* Số byte recvfrom() đã nhận */

    received = recvfrom(socket_fd, buffer, MAX_TEXT - 1, 0, NULL, NULL);
    if (received < 0) {
        return 0;
    }
    buffer[received] = '\0';
    return 1;
}

/**
 * @brief Đọc một dòng từ bàn phím và loại bỏ ký tự xuống dòng.
 *
 * @param[out] buffer Bộ đệm nhận dòng nhập từ stdin.
 * @return 1 nếu đọc thành công, 0 nếu gặp EOF hoặc lỗi đọc.
 */
int read_line(char buffer[MAX_TEXT])
{
    if (fgets(buffer, MAX_TEXT, stdin) == NULL) {
        return 0;
    }
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 1;
}
