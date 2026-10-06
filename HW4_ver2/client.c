/*
 * UDP Chat Client (HW4 - IT4062)
 *
 * Cách chạy:  ./client IPAddress PortNumber     (ví dụ: ./client 127.0.0.1 5500)
 *
 * Chức năng:
 *   - Gửi gói tin __CONNECT__ để đăng ký với server.
 *   - Dùng select() theo dõi đồng thời bàn phím và socket:
 *       + Dòng nhập từ bàn phím -> gửi lên server.
 *       + Gói tin từ server (tin của client kia / thông báo lỗi) -> hiển thị.
 *   - Lặp lại cho đến khi người dùng nhập "@" hoặc "#" (thoát với mã 0).
 */
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ===================== Hằng số dùng chung ===================== */
#define BUFF_SIZE       1024              /* Kích thước tối đa của 1 datagram / 1 dòng nhập (byte) */
#define CONNECT_TOKEN   "__CONNECT__"     /* Gói tin khởi tạo client gửi khi bắt đầu phiên */
#define EXIT_TOKEN_1    "@"               /* Xâu kết thúc thứ nhất */
#define EXIT_TOKEN_2    "#"               /* Xâu kết thúc thứ hai */

/* ===================== Hàm tiện ích ===================== */

/**
 * @brief Chuyển xâu địa chỉ IPv4 sang struct in_addr.
 *
 * Cài đặt: gọi inet_pton(AF_INET, str, addr). Hàm này trả về 1 khi thành công,
 * 0 khi xâu không đúng định dạng IPv4 và -1 khi lỗi hệ thống; ở đây mọi
 * trường hợp khác 1 đều quy về mã lỗi -1 của hàm.
 *
 * @param[in]  str   Xâu IPv4 dạng "a.b.c.d"; có thể là NULL.
 * @param[out] addr  Nhận địa chỉ (network byte order) khi hợp lệ.
 *
 * @return Mã kết quả (int):
 *         - `0`: Hợp lệ, `*addr` đã được gán.
 *         - `-1`: NULL hoặc không phải địa chỉ IPv4 hợp lệ.
 */
int parse_ip(const char *str, struct in_addr *addr)
{
    if (str == NULL)
        return -1;
    return (inet_pton(AF_INET, str, addr) == 1) ? 0 : -1;
}

/**
 * @brief Chuyển xâu số hiệu cổng thành số nguyên 16 bit (có kiểm tra chặt chẽ).
 *
 * Cài đặt:
 *   - Từ chối xâu NULL hoặc rỗng.
 *   - Đặt errno = 0 trước khi gọi strtol() để phát hiện lỗi tràn số (ERANGE).
 *   - `*end != '\0'` nghĩa là còn ký tự thừa sau phần số (ví dụ "80abc") -> sai.
 *   - Chỉ chấp nhận giá trị trong 1..65535 (cổng 0 và số âm bị loại).
 *
 * @param[in]  str   Xâu cần chuyển đổi; có thể là NULL.
 * @param[out] port  Nhận số hiệu cổng khi hợp lệ; không đổi nếu lỗi.
 *
 * @return Mã kết quả (int):
 *         - `0`: Hợp lệ, `*port` đã được gán.
 *         - `-1`: Không hợp lệ (NULL/rỗng/không phải số/tràn số/ngoài 1..65535).
 */
int parse_port(const char *str, uint16_t *port)
{
    char *end = NULL;
    long value;

    if (str == NULL || *str == '\0')
        return -1;
    errno = 0;
    value = strtol(str, &end, 10);
    if (errno != 0 || *end != '\0' || value < 1 || value > 65535)
        return -1;
    *port = (uint16_t)value;
    return 0;
}

/**
 * @brief Xóa các ký tự '\n' và '\r' ở cuối xâu, sửa trực tiếp trên xâu.
 *
 * Cài đặt: tính độ dài một lần, rồi lùi dần từ cuối và ghi '\0' đè lên từng
 * ký tự xuống dòng liên tiếp. Nhờ vậy xử lý được cả "\n", "\r\n" và "\r\n\r\n".
 *
 * @param[in,out] str  Xâu kết thúc bằng '\0', được sửa tại chỗ; không được NULL.
 *
 * @return Không có (void).
 */
void strip_newline(char *str)
{
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r'))
        str[--len] = '\0';
}

/**
 * @brief Kiểm tra xâu có đúng bằng "@" hoặc "#" hay không.
 *
 * Cài đặt: dùng strcmp() so sánh toàn bộ xâu với hai token kết thúc, do đó
 * chỉ khớp khi xâu chứa đúng một ký tự đó.
 *
 * @param[in] str  Xâu kết thúc bằng '\0' đã loại bỏ ký tự xuống dòng; không được NULL.
 *
 * @return Kết quả (int):
 *         - `1`: Là xâu kết thúc.
 *         - `0`: Không phải xâu kết thúc.
 */
int is_exit_token(const char *str)
{
    return strcmp(str, EXIT_TOKEN_1) == 0 || strcmp(str, EXIT_TOKEN_2) == 0;
}

#define PROMPT "[You]: "      /* Dấu nhắc nhập liệu hiển thị cho người dùng */

/**
 * @brief In dấu nhắc "[You]: " và đẩy ngay ra màn hình.
 *
 * Gọi fflush() vì dấu nhắc không có ký tự xuống dòng; nếu không, khi stdout
 * là pipe/file (bị đệm hoàn toàn) hoặc terminal (đệm theo dòng) thì dấu nhắc
 * sẽ không xuất hiện kịp thời.
 *
 * @return Không có (void).
 */
static void prompt(void)
{
    fputs(PROMPT, stdout);
    fflush(stdout);
}

/**
 * @brief Gửi một xâu văn bản tới server qua socket UDP đã connect().
 *
 * Gửi đúng strlen(msg) byte (không kèm '\0') trong một datagram bằng send().
 * Nếu lỗi, in thông báo bằng perror() nhưng không kết thúc chương trình.
 *
 * @param[in] sock  File descriptor của socket UDP (đã connect() tới server).
 * @param[in] msg   Xâu kết thúc bằng '\0' cần gửi.
 *
 * @return Không có (void).
 */
static void send_msg(int sock, const char *msg)
{
    if (send(sock, msg, strlen(msg), 0) < 0)
        perror("Error: send");
}

/**
 * @brief Hàm chính thực thi UDP Chat Client.
 *
 * Luồng xử lý:
 *   - Bước 0: Kiểm tra tham số (đúng 2 tham số: IPv4 hợp lệ và cổng 1..65535).
 *   - Bước 1: Tạo socket UDP và connect() để cố định địa chỉ đích (UDP chỉ lưu
 *             địa chỉ, không bắt tay; đồng thời lọc gói từ nguồn khác và cho phép
 *             nhận lỗi ECONNREFUSED khi server không chạy).
 *   - Bước 2: Gửi __CONNECT__ đăng ký, in "Connected to server ...".
 *   - Bước 3: Vòng lặp select() trên stdin và socket:
 *       + Socket sẵn sàng: nhận và hiển thị tin (tin client kia, lỗi, thông báo);
 *         nếu server báo đầy thì thoát với mã 1.
 *       + stdin sẵn sàng: đọc thô bằng read(), tự tách thành từng dòng (tránh
 *         vấn đề bộ đệm stdio làm select() bỏ sót dữ liệu). Dòng rỗng chỉ hiện lại
 *         dấu nhắc; "@"/"#" được gửi rồi thoát với mã 0; dòng khác được gửi lên server.
 *         Dòng dài hơn BUFF_SIZE-1 ký tự bị từ chối và bỏ phần còn lại.
 *         Gặp EOF trên stdin thì gửi "@" và thoát với mã 0.
 *
 * @param[in] argc  Số lượng tham số dòng lệnh (phải bằng 3).
 * @param[in] argv  Mảng tham số: argv[1] là IPv4 của server, argv[2] là số hiệu cổng.
 *
 * @return Mã thoát chương trình (int):
 *         - `0`: Người dùng nhập "@"/"#" hoặc stdin kết thúc (EOF).
 *         - `1`: Sai tham số, lỗi socket()/connect()/select(), không tới được server,
 *                hoặc server báo đầy.
 */
int main(int argc, char *argv[])
{
    uint16_t port;
    struct sockaddr_in server_addr;
    int sock;
    char inbuf[BUFF_SIZE];       /* Bộ đệm dòng đang nhập */
    size_t inlen = 0;            /* Số ký tự hiện có trong inbuf */
    int discarding = 0;          /* 1: đang bỏ phần thừa của dòng quá dài */

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IPAddress> <PortNumber>\n", argv[0]);
        return 1;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    if (parse_ip(argv[1], &server_addr.sin_addr) != 0) {
        fprintf(stderr, "Usage: %s <IPAddress> <PortNumber>\n", argv[0]);
        fprintf(stderr, "Error: Invalid IPv4 address '%s'.\n", argv[1]);
        return 1;
    }
    if (parse_port(argv[2], &port) != 0) {
        fprintf(stderr, "Usage: %s <IPAddress> <PortNumber>\n", argv[0]);
        fprintf(stderr, "Error: Invalid port number '%s' (must be 1-65535).\n", argv[2]);
        return 1;
    }
    server_addr.sin_port = htons(port);

    /* Step 1: Tạo socket UDP và "connect" (chỉ để cố định địa chỉ đích) */
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error: socket");
        return 1;
    }
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error: connect");
        close(sock);
        return 1;
    }

    /* Step 2: Đăng ký với server */
    send_msg(sock, CONNECT_TOKEN);
    printf("Connected to server %s at port %u\n", argv[1], (unsigned)port);
    prompt();

    /* Step 3: Vòng lặp I/O multiplexing */
    for (;;) {
        fd_set rfds;
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sock, &rfds);

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR)
                continue;
            perror("Error: select");
            close(sock);
            return 1;
        }

        /* --- Có dữ liệu từ server --- */
        if (FD_ISSET(sock, &rfds)) {
            char rbuf[BUFF_SIZE + 64];
            ssize_t n = recv(sock, rbuf, sizeof(rbuf) - 1, 0);
            if (n < 0) {
                if (errno == ECONNREFUSED)
                    fprintf(stderr, "\nError: Cannot reach server %s:%u.\n", argv[1], (unsigned)port);
                else
                    perror("\nError: recv");
                close(sock);
                return 1;
            }
            rbuf[n] = '\0';
            printf("\n%s\n", rbuf);
            if (strncmp(rbuf, "Error: Server is full", 21) == 0) {
                close(sock);
                return 1;
            }
            prompt();
        }

        /* --- Có dữ liệu từ bàn phím --- */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char chunk[BUFF_SIZE];
            ssize_t r = read(STDIN_FILENO, chunk, sizeof(chunk));
            int eof = (r <= 0);
            ssize_t i;

            if (eof) {
                /* Hết dữ liệu nhập: coi như người dùng thoát */
                send_msg(sock, EXIT_TOKEN_1);
                printf("\nDisconnected from server. Bye!\n");
                close(sock);
                return 0;
            }

            for (i = 0; i < r; i++) {
                char c = chunk[i];
                if (c != '\n') {
                    if (discarding)
                        continue;
                    if (inlen < sizeof(inbuf) - 1) {
                        inbuf[inlen++] = c;
                    } else {
                        discarding = 1;
                        inlen = 0;
                        printf("Error: Input too long (max %d characters).\n", BUFF_SIZE - 1);
                    }
                    continue;
                }

                /* Gặp ký tự xuống dòng: xử lý một dòng hoàn chỉnh */
                if (discarding) {
                    discarding = 0;
                    inlen = 0;
                    prompt();
                    continue;
                }
                inbuf[inlen] = '\0';
                inlen = 0;
                strip_newline(inbuf);

                if (inbuf[0] == '\0') {          /* Dòng rỗng: chỉ hiện lại prompt */
                    prompt();
                    continue;
                }
                send_msg(sock, inbuf);
                if (is_exit_token(inbuf)) {
                    printf("Disconnected from server. Bye!\n");
                    close(sock);
                    return 0;
                }
                prompt();
            }
        }
    }
}
