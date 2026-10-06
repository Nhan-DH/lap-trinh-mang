/*
 * UDP Chat Server (HW4 - IT4062)
 *
 * Cách chạy:  ./server PortNumber      (ví dụ: ./server 5500)
 *
 * Chức năng:
 *   - Quản lý tối đa 2 client (2 "slot"). Client đăng ký bằng gói tin __CONNECT__.
 *   - Nhận xâu từ 1 client:
 *       + Chứa ký tự không phải chữ cái/chữ số -> gửi thông báo lỗi về chính client đó,
 *         KHÔNG hiển thị trên server, KHÔNG chuyển tiếp.
 *       + Hợp lệ -> hiển thị trên server và chuyển tiếp cho client còn lại
 *         kèm IP:Port của client gửi.
 *       + "@" hoặc "#" -> giải phóng slot của client đó.
 *   - Client thứ 3 khi server đầy sẽ nhận thông báo "Server is full".
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ===================== Hằng số dùng chung ===================== */
#define BUFF_SIZE       1024              /* Kích thước tối đa của 1 datagram / 1 dòng nhập (byte) */
#define MAX_CLIENTS     2                 /* Số client tối đa server phục vụ cùng lúc (2 slot) */
#define CONNECT_TOKEN   "__CONNECT__"     /* Gói tin khởi tạo client gửi khi bắt đầu phiên */
#define EXIT_TOKEN_1    "@"               /* Xâu kết thúc thứ nhất */
#define EXIT_TOKEN_2    "#"               /* Xâu kết thúc thứ hai */
#define MSG_INVALID_CHARS \
    "Error: String contains invalid characters! Only alphanumeric characters are allowed."
#define MSG_SERVER_FULL \
    "Error: Server is full (maximum 2 clients reached)."
#define MSG_NOT_CONNECTED \
    "Error: You are not connected to the server. Please restart the client."
#define MSG_NO_PEER \
    "Info: No other client is connected yet. Your message was not delivered."
#define MSG_PEER_LEFT \
    "[Server] The other client has disconnected."

/* ===================== Hàm tiện ích ===================== */

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
 * @brief Kiểm tra xâu chỉ gồm chữ cái và chữ số.
 *
 * Cài đặt: từ chối NULL và xâu rỗng ngay từ đầu, sau đó duyệt từng ký tự
 * và dừng ở ký tự đầu tiên không thỏa isalnum(). Việc ép `unsigned char`
 * là bắt buộc vì isalnum() chỉ xác định với giá trị trong khoảng
 * unsigned char hoặc EOF.
 *
 * @param[in] str  Xâu kết thúc bằng '\0'; có thể là NULL.
 *
 * @return Kết quả (int):
 *         - `1`: Xâu không rỗng và mọi ký tự đều là chữ cái/chữ số.
 *         - `0`: NULL, rỗng hoặc có ký tự không hợp lệ.
 */
int is_alnum_string(const char *str)
{
    if (str == NULL || *str == '\0')
        return 0;
    for (; *str != '\0'; str++) {
        if (!isalnum((unsigned char)*str))
            return 0;
    }
    return 1;
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

/**
 * @brief Định dạng địa chỉ socket IPv4 thành xâu "ip:port".
 *
 * Cài đặt: inet_ntop() đổi phần IP sang dạng thập phân có dấu chấm; cổng được
 * đổi từ network byte order sang host byte order bằng ntohs() rồi ghép vào
 * xâu qua snprintf() (luôn kết thúc bằng '\0', không tràn bộ đệm).
 *
 * @param[in]  addr  Địa chỉ socket IPv4 cần định dạng.
 * @param[out] buf   Bộ đệm nhận kết quả.
 * @param[in]  size  Kích thước `buf` (byte); nên >= 22.
 *
 * @return Không có (void).
 */
void format_addr(const struct sockaddr_in *addr, char *buf, size_t size)
{
    char ip[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)) == NULL)
        strcpy(ip, "?.?.?.?");
    snprintf(buf, size, "%s:%u", ip, (unsigned)ntohs(addr->sin_port));
}

/**
 * @brief Thông tin một "slot" (chỗ) dành cho một client đang kết nối.
 *
 * Server có MAX_CLIENTS slot; client được nhận diện bằng cặp IP:Port nguồn
 * của datagram UDP (UDP không có khái niệm kết nối).
 */
typedef struct {
    int active;                  /* 1 nếu slot đang có client, 0 nếu trống */
    struct sockaddr_in addr;     /* Địa chỉ (IP:Port) của client trong slot */
} slot_t;

static slot_t slots[MAX_CLIENTS];               /* Bảng slot của server */
static volatile sig_atomic_t running = 1;       /* Cờ vòng lặp chính; đặt về 0 bởi signal handler */

/**
 * @brief Bộ xử lý tín hiệu SIGINT / SIGTERM.
 *
 * Chỉ đặt cờ `running = 0` (thao tác an toàn trong signal handler). Vì handler
 * được cài đặt không kèm SA_RESTART nên recvfrom() đang chặn sẽ trả về -1 với
 * errno = EINTR, vòng lặp chính kiểm tra cờ và thoát sạch sẽ.
 *
 * @param[in] sig  Số hiệu tín hiệu nhận được (không sử dụng).
 *
 * @return Không có (void).
 */
static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

/**
 * @brief Tìm slot đang được cấp cho client có địa chỉ `addr`.
 *
 * So sánh cả địa chỉ IP và số hiệu cổng nguồn với từng slot đang active.
 *
 * @param[in] addr  Địa chỉ nguồn của datagram vừa nhận (từ recvfrom).
 *
 * @return Chỉ số slot (int):
 *         - `0..MAX_CLIENTS-1`: Client đã đăng ký, trả về chỉ số slot của nó.
 *         - `-1`: Client chưa đăng ký.
 */
static int find_slot(const struct sockaddr_in *addr)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (slots[i].active &&
            slots[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            slots[i].addr.sin_port == addr->sin_port)
            return i;
    }
    return -1;
}

/**
 * @brief Tìm slot trống đầu tiên (chưa có client).
 *
 * Duyệt theo thứ tự nên slot có chỉ số nhỏ được tái sử dụng trước; nhờ vậy
 * khi Client 1 thoát, client mới vào sẽ lại là "Client 1".
 *
 * @return Chỉ số slot (int):
 *         - `0..MAX_CLIENTS-1`: Chỉ số slot trống đầu tiên.
 *         - `-1`: Tất cả slot đã đầy.
 */
static int find_free_slot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (!slots[i].active)
            return i;
    return -1;
}

/**
 * @brief Gửi một xâu văn bản tới địa chỉ chỉ định qua socket UDP.
 *
 * Gửi đúng strlen(text) byte (không kèm '\0') trong một datagram bằng sendto().
 * Nếu gửi lỗi, in thông báo lỗi ra stderr bằng perror() và không dừng server.
 *
 * @param[in] sock  File descriptor của socket UDP server.
 * @param[in] addr  Địa chỉ (IP:Port) của client nhận.
 * @param[in] text  Xâu kết thúc bằng '\0' cần gửi.
 *
 * @return Không có (void).
 */
static void send_text(int sock, const struct sockaddr_in *addr, const char *text)
{
    if (sendto(sock, text, strlen(text), 0,
               (const struct sockaddr *)addr, sizeof(*addr)) < 0)
        perror("[SERVER] sendto");
}

/**
 * @brief Hàm chính thực thi UDP Chat Server.
 *
 * Luồng xử lý:
 *   - Bước 0: Kiểm tra tham số dòng lệnh (đúng 1 tham số là cổng 1..65535),
 *             cài đặt handler cho SIGINT/SIGTERM.
 *   - Bước 1: Tạo socket UDP (AF_INET, SOCK_DGRAM).
 *   - Bước 2: bind() vào INADDR_ANY:port.
 *   - Bước 3: Vòng lặp recvfrom(); với mỗi datagram:
 *       + Client chưa đăng ký: nếu là __CONNECT__ thì cấp slot (hoặc báo server đầy);
 *         gói khác thì báo chưa kết nối (trừ "@"/"#" bị bỏ qua).
 *       + Client đã đăng ký: bỏ qua __CONNECT__ trùng và dòng rỗng; "@"/"#" giải phóng
 *         slot và báo cho client còn lại; xâu không hợp lệ chỉ báo lỗi về người gửi
 *         (không in ra server); xâu hợp lệ được in ra server và chuyển tiếp cho client kia
 *         (hoặc báo chưa có client kia).
 *
 * @param[in] argc  Số lượng tham số dòng lệnh (phải bằng 2).
 * @param[in] argv  Mảng tham số: argv[1] là số hiệu cổng lắng nghe.
 *
 * @return Mã thoát chương trình (int):
 *         - `0`: Server kết thúc bình thường (nhận SIGINT/SIGTERM).
 *         - `1`: Sai tham số dòng lệnh, hoặc lỗi socket()/bind().
 */
int main(int argc, char *argv[])
{
    uint16_t port;
    int server_sock;
    struct sockaddr_in server;
    struct sigaction sa;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <PortNumber>\n", argv[0]);
        return 1;
    }
    if (parse_port(argv[1], &port) != 0) {
        fprintf(stderr, "Usage: %s <PortNumber>\n", argv[0]);
        fprintf(stderr, "Error: Invalid port number '%s' (must be 1-65535).\n", argv[1]);
        return 1;
    }

    /* Ctrl+C / SIGTERM: thoát vòng lặp một cách sạch sẽ (không dùng SA_RESTART) */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Step 1: Tạo socket UDP */
    if ((server_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error: socket");
        return 1;
    }

    /* Step 2: Bind vào INADDR_ANY:port */
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Error: bind");
        close(server_sock);
        return 1;
    }

    printf("[Server] UDP Server is running on port %u...\n", (unsigned)port);
    fflush(stdout);

    /* Step 3: Vòng lặp nhận / xử lý / chuyển tiếp */
    while (running) {
        char buff[BUFF_SIZE];
        char out[BUFF_SIZE + 64];
        char addr_str[32];
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        ssize_t n;
        int idx;

        n = recvfrom(server_sock, buff, sizeof(buff) - 1, 0,
                     (struct sockaddr *)&client, &len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("[SERVER] recvfrom");
            continue;
        }
        buff[n] = '\0';
        strip_newline(buff);
        format_addr(&client, addr_str, sizeof(addr_str));
        idx = find_slot(&client);

        /* --- Client chưa đăng ký --- */
        if (idx < 0) {
            if (strcmp(buff, CONNECT_TOKEN) == 0) {
                int free_idx = find_free_slot();
                if (free_idx < 0) {
                    send_text(server_sock, &client, MSG_SERVER_FULL);
                } else {
                    slots[free_idx].active = 1;
                    slots[free_idx].addr = client;
                    printf("[Server] Client %d connected from %s\n", free_idx + 1, addr_str);
                    fflush(stdout);
                }
            } else if (!is_exit_token(buff)) {
                send_text(server_sock, &client, MSG_NOT_CONNECTED);
            }
            continue;
        }

        /* --- Client đã đăng ký --- */
        if (strcmp(buff, CONNECT_TOKEN) == 0 || buff[0] == '\0')
            continue;                                   /* bỏ qua gói trùng / dòng rỗng */

        if (is_exit_token(buff)) {
            int other = 1 - idx;
            printf("[Server] Client %d (%s) disconnected (exit token '%s'). Slot is freed.\n",
                   idx + 1, addr_str, buff);
            fflush(stdout);
            slots[idx].active = 0;
            if (slots[other].active)
                send_text(server_sock, &slots[other].addr, MSG_PEER_LEFT);
            continue;
        }

        if (!is_alnum_string(buff)) {
            /* Xâu không hợp lệ: chỉ báo lỗi về client gửi, KHÔNG in ra server */
            send_text(server_sock, &client, MSG_INVALID_CHARS);
            continue;
        }

        /* Xâu hợp lệ: hiển thị trên server và chuyển tiếp cho client còn lại */
        printf("[Client %d (%s)]: %s\n", idx + 1, addr_str, buff);
        fflush(stdout);
        {
            int other = 1 - idx;
            if (slots[other].active) {
                snprintf(out, sizeof(out), "[%s]: %s", addr_str, buff);
                send_text(server_sock, &slots[other].addr, out);
            } else {
                send_text(server_sock, &client, MSG_NO_PEER);
            }
        }
    }

    printf("[Server] Shutting down.\n");
    fflush(stdout);
    close(server_sock);
    return 0;
}
