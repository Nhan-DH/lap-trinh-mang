/*
 * Module: client.c
 * Chức năng: Điều phối luồng UDP client: nhập username/password, mã hóa và thoát.
 * Được gọi bởi: hệ điều hành khi chạy ./client <IPAddress> <PortNumber>.
 * Gọi tới: client_io.c để kiểm tra port, đọc stdin và gửi/nhận datagram.
 */
#include <stdio.h>          /* Thư viện vào/ra chuẩn: printf, fprintf, perror, fgets */
#include <stdlib.h>         /* Thư viện chuẩn: strtol, EXIT_SUCCESS, EXIT_FAILURE */
#include <string.h>         /* strcmp và memset xử lý dữ liệu chính */
#include <unistd.h>         /* Các thao tác POSIX: close */
#include <sys/socket.h>     /* Các hàm UDP: socket, sendto, recvfrom */
#include <netinet/in.h>     /* Cấu trúc địa chỉ IPv4: sockaddr_in */
#include <arpa/inet.h>      /* inet_pton() và htons() xử lý IPv4, byte order */
#include "client_io.h"      /* Các hàm nhập liệu và gửi nhận UDP */

/**
 * @brief Khởi tạo UDP client và thực hiện đăng nhập, mã hóa, đăng xuất.
 *
 * Chi tiết luồng xử lý:
 *   - Bước 1: Kiểm tra địa chỉ IPv4 và port từ dòng lệnh.
 *   - Bước 2: Tạo socket IPv4 dạng SOCK_DGRAM của UDP.
 *   - Bước 3: Thiết lập địa chỉ server bằng inet_pton() và htons().
 *   - Bước 4: Gửi username, password và nhận phản hồi xác thực.
 *   - Bước 5: Gửi chuỗi sau đăng nhập để server tính SHA-256.
 *   - Bước 6: Gửi bye hoặc datagram rỗng để kết thúc phiên.
 *   - Bước 7: Đóng socket và kết thúc an toàn.
 *
 * @param[in] argc Số lượng đối số dòng lệnh, phải bằng 3.
 * @param[in] argv argv[1] là IPv4 server, argv[2] là port server.
 * @return int:
 *   - 0 nếu client kết thúc phiên bình thường.
 *   - EXIT_FAILURE nếu tham số, socket hoặc giao tiếp bị lỗi.
 */
int main(int argc, char *argv[])
{
    int socket_fd;                     /* File descriptor của socket client */
    int port;                          /* Port server ở dạng host byte order */
    struct sockaddr_in server_address; /* Địa chỉ IPv4 và port của server */
    char username[MAX_TEXT];           /* Username người dùng nhập */
    char password[MAX_TEXT];           /* Password người dùng nhập */
    char message[MAX_TEXT];             /* Bộ đệm dữ liệu gửi và nhận */
    int logged_in = 0;                  /* 1 khi server trả về OK */

    /* =========================================================================
     * Step 1: Kiểm tra tham số và địa chỉ IPv4
     * =========================================================================
     */
    memset(&server_address, 0, sizeof(server_address));
    if (argc != 3 || !parse_port(argv[2], &port) ||
        inet_pton(AF_INET, argv[1], &server_address.sin_addr) != 1) {
        fprintf(stderr, "Usage: %s <IPAddress> <PortNumber>\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* =========================================================================
     * Step 2: Tạo socket UDP IPv4
     * =========================================================================
     * socket() tạo endpoint mạng.
     *   - AF_INET: sử dụng địa chỉ IPv4.
     *   - SOCK_DGRAM: sử dụng datagram UDP, không cần connect TCP.
     *   - 0: chọn protocol mặc định của SOCK_DGRAM.
     *   - Kết quả: file descriptor không âm nếu thành công, -1 nếu lỗi.
     */
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        perror("socket"); /* perror() in mô tả lỗi system call dựa trên errno. */
        return EXIT_FAILURE;
    }

    /* =========================================================================
     * Step 3: Thiết lập địa chỉ server
     * =========================================================================
     */
    server_address.sin_family = AF_INET;
    /* htons() đổi port từ host byte order sang network byte order. */
    server_address.sin_port = htons((unsigned short)port);

    /* =========================================================================
     * Step 4: Đăng nhập và nhận phản hồi xác thực
     * =========================================================================
     */
    while (!logged_in) {
        printf("Username: ");
        fflush(stdout);
        if (!read_line(username)) {
            close(socket_fd);
            return EXIT_SUCCESS;
        }
        if (!send_message(socket_fd, username, &server_address) ||
            !receive_message(socket_fd, message)) {
            perror("communication");
            close(socket_fd);
            return EXIT_FAILURE;
        }
        printf("%s\n", message);
        if (strcmp(message, "Insert password") != 0) {
            continue;
        }

        printf("Password: ");
        fflush(stdout);
        if (!read_line(password)) {
            close(socket_fd);
            return EXIT_SUCCESS;
        }
        if (!send_message(socket_fd, password, &server_address) ||
            !receive_message(socket_fd, message)) {
            perror("communication");
            close(socket_fd);
            return EXIT_FAILURE;
        }
        printf("%s\n", message);
        if (strcmp(message, "OK") == 0) {
            logged_in = 1;
            break;
        }
    }

    /* =========================================================================
     * Step 5: Gửi chuỗi và nhận mã SHA-256 hoặc Error
     * =========================================================================
     */
    while (logged_in) {
        printf("Input: ");
        fflush(stdout);
        if (!read_line(message)) {
            (void)send_message(socket_fd, "bye", &server_address);
            if (receive_message(socket_fd, message)) {
                printf("%s\n", message);
            }
            break;
        }
        if (message[0] == '\0') {
            if (!send_empty_message(socket_fd, &server_address) ||
                !receive_message(socket_fd, message)) {
                perror("communication");
                close(socket_fd);
                return EXIT_FAILURE;
            }
            printf("%s\n", message);
            break;
        }
        if (strcmp(message, "bye") == 0) {
            if (!send_message(socket_fd, message, &server_address) ||
                !receive_message(socket_fd, message)) {
                perror("communication");
                close(socket_fd);
                return EXIT_FAILURE;
            }
            printf("%s\n", message);
            break;
        }
        if (!send_message(socket_fd, message, &server_address) ||
            !receive_message(socket_fd, message)) {
            perror("communication");
            close(socket_fd);
            return EXIT_FAILURE;
        }
        printf("%s\n", message);
    }

    /* =========================================================================
     * Step 6: Đóng socket và kết thúc client
     * =========================================================================
     * close() giải phóng file descriptor và tài nguyên socket UDP.
     */
    close(socket_fd);
    return EXIT_SUCCESS;
}
