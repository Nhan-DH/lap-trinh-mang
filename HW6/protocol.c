#include "protocol.h"              /* Khai báo các hàm giao thức TCP. */

#include <arpa/inet.h>             /* htonl, ntohl chuyển byte order. */
#include <errno.h>                 /* errno và EINTR khi system call bị ngắt. */
#include <sys/socket.h>            /* send và recv. */

int send_all(int sockfd, const void *buffer, size_t count)
{
    const char *data = buffer;     /* Con trỏ byte để gửi tuần tự. */
    size_t total = 0;              /* Tổng byte đã gửi. */

    while (total < count) {
        /*
         * send(): ghi dữ liệu vào TCP stream.
         *   - sockfd: socket đã kết nối.
         *   - data + total: phần dữ liệu chưa gửi.
         *   - count - total: số byte còn lại.
         *   - 0: không dùng cờ đặc biệt.
         * Trả số byte đã gửi; -1 nếu lỗi.
         */
        ssize_t result = send(sockfd, data + total, count - total, 0);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return -1;
        }
        total += (size_t)result;
    }
    return 0;
}

int recv_all(int sockfd, void *buffer, size_t count)
{
    char *data = buffer;            /* Vùng nhớ ghi dữ liệu nhận được. */
    size_t total = 0;              /* Tổng byte đã nhận. */

    while (total < count) {
        /*
         * recv(): đọc dữ liệu từ TCP stream.
         * Trả số byte thực tế, 0 khi peer đóng kết nối, -1 khi lỗi.
         * Vì TCP không giữ ranh giới gói, phải lặp tới khi đủ count byte.
         */
        ssize_t result = recv(sockfd, data + total, count - total, 0);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return -1;
        }
        total += (size_t)result;
    }
    return 0;
}

int recv_line(int sockfd, char *line, size_t capacity)
{
    size_t index = 0;               /* Vị trí ghi ký tự tiếp theo. */

    if (capacity == 0) {
        return -1;
    }
    while (index + 1 < capacity) {
        char character;             /* Một byte đọc từ TCP stream. */
        ssize_t result;
        /*
         * recv() đọc đúng một byte để tìm '\n'. Đây là cách đơn giản,
         * dễ quan sát cho giao thức header dạng dòng của bài thực hành.
         */
        result = recv(sockfd, &character, 1, 0);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return -1;
        }
        if (character == '\n') {
            line[index] = '\0';
            return (int)index;
        }
        line[index++] = character;
    }
    return -1;                      /* Header quá dài, tránh tràn bộ đệm. */
}

uint64_t host_to_network64(uint64_t value)
{
    uint32_t high = htonl((uint32_t)(value >> 32)); /* 32 bit cao. */
    uint32_t low = htonl((uint32_t)value);           /* 32 bit thấp. */
    return ((uint64_t)low << 32) | high;
}

uint64_t network_to_host64(uint64_t value)
{
    uint32_t high = ntohl((uint32_t)(value & 0xffffffffU)); /* Phần cao. */
    uint32_t low = ntohl((uint32_t)(value >> 32));           /* Phần thấp. */
    return ((uint64_t)high << 32) | low;
}
