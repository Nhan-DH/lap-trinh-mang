/*
 * Module: server.c
 * Chức năng: Chứa main() và điều phối vòng đời UDP server.
 * Gọi tới: server_account.c để đọc tài khoản; server_session.c để quản lý
 *           phiên; server_protocol.c để xử lý từng datagram.
 * Được gọi bởi: hệ điều hành khi chạy ./server <PortNumber>.
 */
#include <stdio.h>          /* printf, fprintf và perror */
#include <stdlib.h>         /* EXIT_SUCCESS và EXIT_FAILURE */
#include <string.h>         /* memset xóa cấu trúc địa chỉ */
#include <errno.h>          /* errno và EINTR của recvfrom */
#include <signal.h>         /* signal và SIGINT dừng server */
#include <unistd.h>         /* close đóng socket */
#include <sys/socket.h>     /* socket, bind và recvfrom */
#include <netinet/in.h>     /* struct sockaddr_in, INADDR_ANY */
#include "server.h"         /* Kiểu dữ liệu và prototype của server */

static volatile sig_atomic_t server_running = 1; /* Cờ điều khiển server */

/**
 * @brief Đặt cờ dừng server khi nhận tín hiệu kết thúc.
 *
 * @param[in] signal_number Mã tín hiệu do hệ điều hành truyền vào.
 */
static void handle_signal(int signal_number)
{
    (void)signal_number;
    server_running = 0;
}

/**
 * @brief Khởi tạo UDP server và xử lý nhiều phiên client độc lập.
 *
 * Chi tiết luồng xử lý:
 *   - Bước 1: Kiểm tra port và đọc cơ sở dữ liệu account.txt.
 *   - Bước 2: Tạo socket IPv4 dạng SOCK_DGRAM của UDP.
 *   - Bước 3: Thiết lập địa chỉ bằng struct sockaddr_in và bind socket.
 *   - Bước 4: Nhận datagram bằng recvfrom() và xác định phiên theo IP, port.
 *   - Bước 5: Xử lý đăng nhập, SHA-256 hoặc đăng xuất rồi trả lời bằng sendto().
 *   - Bước 6: Đóng socket khi server nhận tín hiệu dừng.
 *
 * @param[in] argc Số lượng đối số dòng lệnh, phải bằng 2.
 * @param[in] argv argv[1] chứa port server dạng chuỗi.
 * @return int:
 *   - 0 nếu server kết thúc bình thường.
 *   - EXIT_FAILURE nếu tham số, file hoặc system call khởi tạo bị lỗi.
 */
int main(int argc, char *argv[])
{
    int socket_fd;                    /* File descriptor của socket UDP */
    int port;                         /* Port ở dạng host byte order */
    struct sockaddr_in server_address; /* Địa chỉ IPv4 mà server bind */
    struct sockaddr_in client_address; /* Địa chỉ client do recvfrom cung cấp */
    socklen_t client_length;          /* Kích thước cấu trúc địa chỉ client */
    char message[MAX_TEXT];            /* Bộ đệm chứa datagram nhận được */
    ssize_t received;                  /* Số byte recvfrom đã nhận */
    Account accounts[MAX_ACCOUNTS];    /* Danh sách tài khoản trong file */
    size_t account_count;              /* Số tài khoản thực tế */
    Session sessions[MAX_SESSIONS] = {{0}}; /* Bảng phiên theo IP và port */
    Session *session;                  /* Phiên của client hiện tại */

    /* =========================================================================
     * Step 1: Kiểm tra tham số và đọc cơ sở dữ liệu tài khoản
     * =========================================================================
     */
    if (argc != 2 || !parse_port(argv[1], &port)) {
        fprintf(stderr, "Usage: %s <PortNumber>\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!load_accounts(accounts, &account_count)) {
        fprintf(stderr, "Cannot open %s\n", ACCOUNT_FILE);
        return EXIT_FAILURE;
    }
    /* =========================================================================
     * Step 2: Tạo socket UDP IPv4
     * =========================================================================
     * socket() tạo endpoint mạng.
     *   - AF_INET: dùng địa chỉ IPv4.
     *   - SOCK_DGRAM: dùng datagram UDP, không cần thiết lập kết nối trước.
     *   - 0: chọn protocol mặc định của SOCK_DGRAM.
     *   - Kết quả: file descriptor không âm nếu thành công, -1 nếu lỗi.
     */
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        perror("socket"); /* perror() in mô tả lỗi system call dựa trên errno. */
        return EXIT_FAILURE;
    }

    /* =========================================================================
     * Step 3: Thiết lập địa chỉ IPv4 và bind socket
     * =========================================================================
     */
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET; /* Họ địa chỉ IPv4 */
    /* htonl() đổi số 32-bit từ host byte order sang network byte order. */
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    /* htons() đổi port 16-bit từ host byte order sang network byte order. */
    server_address.sin_port = htons((unsigned short)port);

    /*
     * bind() gắn socket với địa chỉ và port để server nhận datagram.
     *   - socket_fd: Socket được tạo ở Step 2.
     *   - server_address: Địa chỉ IPv4 và port cần lắng nghe.
     *   - sizeof(server_address): Kích thước cấu trúc địa chỉ.
     *   - Kết quả: 0 nếu thành công, -1 nếu port bị chiếm hoặc có lỗi.
     */
    if (bind(socket_fd, (struct sockaddr *)&server_address,
             sizeof(server_address)) < 0) {
        perror("bind");
        close(socket_fd);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    printf("UDP server listening on port %d\n", port);
    /* =========================================================================
     * Step 4: Nhận và xử lý datagram từ nhiều client
     * =========================================================================
     */
    while (server_running) {
        client_length = sizeof(client_address);
        /*
         * recvfrom() chờ và nhận một datagram UDP từ client bất kỳ.
         *   - socket_fd: Socket đang lắng nghe.
         *   - message: Bộ đệm nhận dữ liệu, chừa một byte cho '\0'.
         *   - sizeof(message) - 1: Số byte tối đa được ghi.
         *   - 0: Flag mặc định.
         *   - client_address: Nơi lưu IP và port của client gửi.
         *   - client_length: Kích thước cấu trúc địa chỉ vào/ra.
         *   - Kết quả: số byte nhận được, 0 nếu datagram rỗng, -1 nếu lỗi.
         */
        received = recvfrom(socket_fd, message, sizeof(message) - 1, 0,
                            (struct sockaddr *)&client_address, &client_length);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            break;
        }
        message[received] = '\0';
        /* Mỗi IP và ephemeral port có một trạng thái đăng nhập riêng. */
        session = get_session(sessions, &client_address);
        if (session == NULL) {
            /* sendto() trả lời client khi bảng phiên đã đạt giới hạn. */
            (void)sendto(socket_fd, "Server busy", strlen("Server busy"), 0,
                         (struct sockaddr *)&client_address, client_length);
            continue;
        }
        process_datagram(socket_fd, session, accounts, account_count, message,
                         &client_address, client_length);
    }
    /* =========================================================================
     * Step 5: Đóng socket và kết thúc server
     * =========================================================================
     * close() giải phóng file descriptor và tài nguyên socket của tiến trình.
     */
    close(socket_fd);
    return EXIT_SUCCESS;
}
