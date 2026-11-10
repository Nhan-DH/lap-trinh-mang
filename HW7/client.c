/*
 * client.c - HW7 TCP client.
 *
 * Client cung cap menu gui chuoi va file. File duoc gui theo sliding window,
 * co ACK tich luy, RTO backoff, fast retransmit, PROBE va mo phong loi.
 */
#define _GNU_SOURCE
#include "proto.h"          /* Hi?u ??c frame v? h?m MD5 d?ng chung */

#include <arpa/inet.h>      /* inet_pton, htons v? ??a ch? IPv4 */
#include <errno.h>          /* errno v? m? l?i h? th?ng l?i g?i */
#include <fcntl.h>          /* open v? c? O_RDONLY */
#include <netinet/tcp.h>    /* TCP_NODELAY tối ưu frame ACK ngắn */
#include <poll.h>           /* poll cho th?i gian ch? ACK v? PROBE */
#include <stdio.h>          /* printf, fprintf, perror, fgets */
#include <stdlib.h>         /* strtoul, srand v? EXIT_* */
#include <signal.h>         /* t?n hi?u, SIGPIPE v? SIG_IGN */
#include <string.h>         /* strlen, memcpy, strcmp, strrchr */
#include <sys/socket.h>     /* socket, k?t n?i v? c?u tr?c socket */
#include <sys/stat.h>       /* st?t v? S_ISREG ki?m tra t?p */
#include <unistd.h>         /* ??ng, ??c, pread v? getpid */

typedef struct {
    /* Tr?ng thai cua mot chunk dang cho ACK trong cua so. */
    long long sent_at;
    int outstanding;
} sent_t;

typedef struct {
    unsigned long long transmissions;
    unsigned long long retransmitted_timeout;
    unsigned long long retransmitted_fast;
    unsigned long long dropped;
    unsigned long long corrupted;
    unsigned long long probes;
} file_stats_t;

/**
 * @brief Phan tich c?ng TCP trong kho?ng 1..65535.
 * @param[in] text Chuoi so c?ng can phan tich.
 * @param[out] value Noi nhan c?ng da chuyen sang unsigned.
 * @return 0 neu hop le, -1 neu chuoi khong phai c?ng TCP hop le.
 */
static int parse_port(const char *text, unsigned *value)
{
    char *end;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' ||
        parsed < 1 || parsed > 65535) {
        return -1;
    }
    *value = (unsigned)parsed;
    return 0;
}

/**
 * @brief Phan tich ty le mo phong loi trong kho?ng 0..100.
 * @param[in] text Chuoi phan tram can phan tich.
 * @param[out] value Noi nhan gia tri phan tram.
 * @return 0 neu hop le, -1 neu gia tri nam ngoai kho?ng cho phep.
 */
static int parse_percent(const char *text, unsigned *value)
{
    char *end;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || parsed > 100) {
        return -1;
    }
    *value = (unsigned)parsed;
    return 0;
}

/**
 * @brief Gui mot chuoi va doi chieu ket qua tach MD5 tu server.
 * @param[in] fd Socket TCP da ket noi den server.
 * @param[in] text Chuoi can gui, ket thuc bang '\0'.
 * @return 0 neu request duoc xu ly, -1 neu ket noi loi.
 */
static int send_string(int fd, const char *text)
{
    frame_hdr_t header;
    uint8_t payload[MAX_PAYLOAD];
    char server_letters[33];
    char server_digits[33];
    char local_letters[33];
    char local_digits[33];
    char md5[33];

    if (send_frame(fd, T_STR, 0, 0, text, (uint32_t)strlen(text)) < 0) {
        return -1;
    }
    if (recv_frame(fd, &header, payload) != RF_OK) {
        return -1;
    }
    if (header.type == T_ERR) {
        printf("%.*s\n", (int)header.len, payload);
        return 0;
    }
    if (header.type != T_LETTERS || header.len >= sizeof(server_letters)) {
        return -1;
    }
    memcpy(server_letters, payload, header.len);
    server_letters[header.len] = '\0';
    if (recv_frame(fd, &header, payload) != RF_OK ||
        header.type != T_DIGITS || header.len >= sizeof(server_digits)) {
        return -1;
    }
    memcpy(server_digits, payload, header.len);
    server_digits[header.len] = '\0';
    md5_buf_hex(text, strlen(text), md5);
    split_md5(md5, local_letters, local_digits);
    printf("Letters: %s\nDigits : %s\nCheck  : %s\n", server_letters,
           server_digits,
           strcmp(server_letters, local_letters) == 0 &&
                   strcmp(server_digits, local_digits) == 0 ?
               "Match" : "Mismatch");
    return 0;
}

/**
 * @brief ??c mot chunk, dong goi T_DATA va gui v?i loi mo phong tuy chon.
 * @param[in] fd Socket TCP da ket noi den server.
 * @param[in] file_fd File descriptor file ngu?n.
 * @param[in] seq So thu tu chunk.
 * @param[in] len So byte d? li?u cua chunk.
 * @param[in] loss Ty le bo frame DATA.
 * @param[in] corrupt Ty le l?m h?ng frame DATA.
 * @return 0 neu gui hoac mo phong bo frame th?nh cung, -1 neu loi I/O.
 */
static int send_data(int fd, int file_fd, uint32_t seq, uint32_t len,
                     unsigned loss, unsigned corrupt, file_stats_t *stats)
{
    uint8_t payload[sizeof(uint32_t) + CHUNK_SIZE];
    ssize_t got = pread(file_fd, payload + sizeof(uint32_t), len,
                        (off_t)seq * CHUNK_SIZE);
    uint8_t frame[HDR_LEN + MAX_PAYLOAD];
    size_t frame_len;

    if (got != (ssize_t)len) {
        return -1;
    }
    put32(payload, seq);
    stats->transmissions++;
    if (chance((double)loss)) {
        stats->dropped++;
        return 0;
    }
    frame_len = build_frame(frame, T_DATA, 0, seq, payload,
                            sizeof(uint32_t) + len);
    if (frame_len == 0) {
        return -1;
    }
    if (chance((double)corrupt)) {
        frame[HDR_LEN + sizeof(uint32_t)] ^= 0x01;
        stats->corrupted++;
    }
    return send_all(fd, frame, frame_len) < 0 ? -1 : 0;
}

/**
 * @brief Tinh kich thuoc chunk theo kich thuoc file va so thu tu.
 * @param[in] file_size K?ch th??c file tinh bang byte.
 * @param[in] seq So thu tu chunk.
 * @return So byte phai doc cua chunk.
 */
static uint32_t chunk_length(uint64_t file_size, uint32_t seq)
{
    uint64_t offset = (uint64_t)seq * CHUNK_SIZE;
    uint64_t remaining = file_size - offset;
    return remaining > CHUNK_SIZE ? CHUNK_SIZE : (uint32_t)remaining;
}

/**
 * @brief Truyen mot file theo giao thuc FBEGIN/DATA/FEND.
 *
 * @param[in] fd Socket TCP da ket noi den server.
 * @param[in] path Duong dan file ngu?n.
 * @param[in] loss Ty le bo frame DATA.
 * @param[in] corrupt Ty le l?m h?ng frame DATA.
 * Client doc file theo pread(), khong gi? ca file trong bo nho. Vong lap
 * truyen xu ly ACK tich luy, cua so nhan, timeout, retransmit va PROBE.
 * @return 0 neu request ho?n t?t, -1 neu loi giao tiep/I/O.
 */
static int send_file(int fd, const char *path, unsigned loss, unsigned corrupt)
/* RTO va bo dem duplicate ACK chi ton t?i trong mot l?n truyen file. */
{
    struct stat info;                 /* Th?ng tin k?ch th??c v? lo?i t?p */
    frame_hdr_t header;               /* Ti?u ?? frame nh?n t? server */
    uint8_t payload[MAX_PAYLOAD];    /* B? ??m d? li?u t?i FBEGIN/ACK/FMD5 */
    const char *name;                 /* T?n t?p basename g?i cho server */
    uint32_t total;                   /* T?ng s? chunk cua t?p */
    uint32_t base = 0;                /* Chunk ??u ti?n ch?a ???c ACK */
    uint32_t next = 0;                /* Chunk ti?p theo ch?a g?i l?n dau */
    uint32_t edge;                    /* Bi?n ph?a tr?n cua sliding c?a s? */
    int file_fd;                      /* T?p b? m? t? t?p ngu?n */
    sent_t sent[MAX_INFLIGHT];        /* Tr?ng th?i t?i ?a 256 chunk ?ang bay */
    md5_t *md5;                       /* Context MD5 cua t?p ngu?n */
    char local_md5[33];               /* MD5 t?p ngu?n d?ng hex in hoa */
    unsigned char buffer[CHUNK_SIZE]; /* B? ??m doc t?p ?? t?nh MD5 */
    ssize_t read_count;               /* S? byte ??c ???c m?i l?n ??c */
    file_stats_t stats = { 0 };
    long long started_at = now_ms();

    if (strchr(path, '/') != NULL || strchr(path, '\\') != NULL ||
        strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
        fprintf(stderr, "Error: invalid file name '%s'.\n", path);
        return 0;
    }
    if (stat(path, &info) < 0) {
        /* st?t() thất bại khi tên tệp không tồn tại hoặc không truy cập được. */
        fprintf(stderr, "Error: File '%s' not found.\n", path);
        return 0;
    }
    if (!S_ISREG(info.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a regular file.\n", path);
        return 0;
    }
    /*
     * Hàm open():
     *   - path: đường dẫn file nguồn người dùng chọn.
     *   - O_RDONLY: chỉ mở để đọc, không thay đổi file gốc.
     *   - Trả về file descriptor >= 0 nếu thành công, -1 nếu lỗi.
     */
    file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return 0;
    }
    name = strrchr(path, '/');
    name = name == NULL ? path : name + 1;
    if (strlen(name) == 0 || strlen(name) > MAX_NAME) {
        fprintf(stderr, "Error: invalid file name\n");
        close(file_fd);
        return 0;
    }
    total = info.st_size == 0 ? 0 :
        (uint32_t)(((uint64_t)info.st_size + CHUNK_SIZE - 1) / CHUNK_SIZE);
    printf("Sending '%s' (%llu bytes, %u chunks of %d bytes)...\n",
           name, (unsigned long long)info.st_size, total, CHUNK_SIZE);
    fflush(stdout);
    md5 = md5_new();
    if (md5 == NULL) {
        close(file_fd);
        return -1;
    }
    while ((read_count = read(file_fd, buffer, sizeof(buffer))) > 0) {
        md5_update(md5, buffer, (size_t)read_count);
    }
    if (read_count < 0) {
        perror("read");
        md5_final_hex(md5, local_md5);
        close(file_fd);
        return -1;
    }
    md5_final_hex(md5, local_md5);
    if (sizeof(uint64_t) + strlen(name) > MAX_PAYLOAD) {
        close(file_fd);
        return 0;
    }
    put64(payload, (uint64_t)info.st_size);
    memcpy(payload + sizeof(uint64_t), name, strlen(name));
    /* Step 1: Gui FBEGIN chua kich thuoc file va ten file. */
    if (send_frame(fd, T_FBEGIN, 0, 0, payload,
                   (uint32_t)(sizeof(uint64_t) + strlen(name))) < 0) {
        close(file_fd);
        return -1;
    }
    {
        int result = recv_frame(fd, &header, payload);
        if (result != RF_OK) {
            close(file_fd);
            return -1;
        }
    }
    if (header.type == T_ERR) {
        printf("%.*s\n", (int)header.len, payload);
        close(file_fd);
        return 0;
    }
    if (header.type != T_ACK) {
        close(file_fd);
        return -1;
    }
    edge = header.seq + header.wnd;
    memset(sent, 0, sizeof(sent));
    {
        unsigned rto = RTO_INIT_MS;
        unsigned duplicate_acks = 0;
        uint32_t last_ack = header.seq;
        uint16_t last_wnd = header.wnd;
        int zero_window = header.wnd == 0;
        long long persist_at = now_ms() + PERSIST_MS;
    /* Step 2: Truyen DATA theo sliding window va xu ly ACK. */
    while (base < total) {
        /* Day cua so bang cac chunk moi cho toi khi cham bien server cho phep. */
        while (next < total && next < edge && next < base + MAX_INFLIGHT) {
            uint32_t len = chunk_length((uint64_t)info.st_size, next);
            if (send_data(fd, file_fd, next, len, loss, corrupt, &stats) < 0) {
                fprintf(stderr, "Error: failed to send DATA chunk %u.\n",
                        next);
                close(file_fd);
                return -1;
            }
            sent[next % MAX_INFLIGHT].outstanding = 1;
            sent[next % MAX_INFLIGHT].sent_at = now_ms();
            next++;
        }
        {
            struct pollfd pfd = { fd, POLLIN, 0 };
            long long now = now_ms();
            long long deadline = now + (base < next ? rto : PERSIST_MS);
            int timeout = (int)(deadline - now);
            /* Khi wnd = 0, chi d?ng persist timer de gui PROBE. */
            if (zero_window && next == edge) {
                timeout = (int)(persist_at - now);
            } else if (base < next) {
                long long retransmit_at =
                    sent[base % MAX_INFLIGHT].sent_at + rto;
                timeout = (int)(retransmit_at - now);
            }
            if (timeout < 0) {
                timeout = 0;
            }
            /*
             * Hàm poll():
             *   - &pfd: theo dõi socket client có dữ liệu ACK hay không.
             *   - 1: số lượng descriptor trong mảng.
             *   - timeout: thời gian chờ trước RTO hoặc PROBE, tính ms.
             *   - Trả về >0 nếu có sự kiện, 0 khi timeout, -1 nếu lỗi.
             */
            int poll_result = poll(&pfd, 1, timeout);
            if (poll_result < 0 && errno == EINTR) {
                continue;
            }
            if (poll_result < 0) {
                perror("poll");
                /* Step 3: Dong file ngu?n sau khi t?t ca chunk da duoc ACK. */
                close(file_fd);
                /* Step 4: Gui FEND va nhan FMD5 de kiem tra tinh toan ven. */
                return -1;
            }
            if (poll_result == 0) {
                if (zero_window && next == edge) {
                    if (send_frame(fd, T_PROBE, 0, base, NULL, 0) < 0) {
                        close(file_fd);
                        return -1;
                    }
                    stats.probes++;
                    persist_at = now_ms() + PERSIST_MS;
                } else if (base < next) {
                    uint32_t retry = base;
                    if (send_data(fd, file_fd, retry,
                                  chunk_length((uint64_t)info.st_size, retry),
                                  loss, corrupt, &stats) < 0) {
                        fprintf(stderr,
                                "Error: failed to retransmit DATA chunk %u.\n",
                                retry);
                        close(file_fd);
                        return -1;
                    }
                    stats.retransmitted_timeout++;
                    sent[retry % MAX_INFLIGHT].sent_at = now_ms();
                    rto = rto < RTO_MAX_MS / 2 ? rto * 2 : RTO_MAX_MS;
                }
                continue;
            }
        }
        {
            int result = recv_frame(fd, &header, payload);
            if (result != RF_OK || header.type != T_ACK) {
                if (result == RF_EOF || result == RF_ERR) {
                    close(file_fd);
                    return -1;
                }
                continue;
            }
        }
        if (header.seq < base) {
            continue;
        }
        /* ACK lap lai 3 l?n la dau hieu chunk base co the da mat. */
        if (header.seq == last_ack && header.wnd == last_wnd) {
            duplicate_acks++;
            if (duplicate_acks >= 3 && base < next) {
                uint32_t retry = base;
                if (send_data(fd, file_fd, retry,
                              chunk_length((uint64_t)info.st_size, retry),
                              loss, corrupt, &stats) < 0) {
                    fprintf(stderr,
                            "Error: failed to fast retransmit DATA chunk %u.\n",
                            retry);
                    close(file_fd);
                    return -1;
                }
                stats.retransmitted_fast++;
                sent[retry % MAX_INFLIGHT].sent_at = now_ms();
                duplicate_acks = 0;
            }
        } else {
            uint32_t acknowledged = header.seq;
            while (base < acknowledged) {
                sent[base % MAX_INFLIGHT].outstanding = 0;
                base++;
            }
            last_ack = header.seq;
            last_wnd = header.wnd;
            duplicate_acks = 0;
            rto = RTO_INIT_MS;
            edge = header.seq + header.wnd;
            zero_window = header.wnd == 0;
            persist_at = now_ms() + PERSIST_MS;
        }
    }
    }
    close(file_fd);
    if (send_frame(fd, T_FEND, 0, total, NULL, 0) < 0) {
        perror("send FEND");
        close(file_fd);
        return -1;
    }
    for (;;) {
        int result = recv_frame(fd, &header, payload);
        if (result != RF_OK) {
            fprintf(stderr,
                    "Error: invalid FMD5 response (result=%d).\n", result);
            return -1;
        }
        if (header.type == T_ACK) {
            continue;
        }
        if (header.type != T_FMD5 || header.len != 32) {
            fprintf(stderr,
                    "Error: expected FMD5, received type=%u len=%u.\n",
                    header.type, header.len);
            return -1;
        }
        break;
    }
    printf("Sent %llu bytes of file '%s' to server.\n",
           (unsigned long long)info.st_size, name);
    printf("MD5: %s\nCheck: %s\n", local_md5,
           memcmp(local_md5, payload, 32) == 0 ? "Match" : "Mismatch");
    printf("Transmissions     : %llu\n"
           "Retransmitted     : %llu timeout, %llu fast retransmit\n"
           "Simulated faults  : %llu dropped, %llu corrupted\n"
           "Zero-window probes: %llu\n"
           "Elapsed           : %.3f s\n",
           stats.transmissions, stats.retransmitted_timeout,
           stats.retransmitted_fast, stats.dropped, stats.corrupted,
           stats.probes, (double)(now_ms() - started_at) / 1000.0);
    fflush(stdout);
    return 0;
}

/**
 * @brief Khoi tao client TCP va ch?y menu tuong tac.
 *
 * Chi tiet luong xu ly:
 *   - Buoc 1: Kiem tra dia chi IPv4, c?ng va tham so mo phong loi.
 *   - Buoc 2: Tao socket TCP IPv4.
 *   - Buoc 3: Thiet lap dia chi server va connect().
 *   - Buoc 4: Hien thi menu, gui chuoi hoac truyen file.
 *   - Buoc 5: Dong socket va ket thuc khi chon Exit hoac gap EOF.
 *
 * @return int:
 *   - 0 neu client ket thuc binh thuong.
 *   - EXIT_FAILURE neu tham so, socket hoac connect() bi loi.
 */
int main(int argc, char **argv)
{
    struct sockaddr_in address; /* ??a ch? IPv4 cua server */
    unsigned port;               /* Cong TCP server */
    unsigned loss = 0;           /* T? l? b? frame D? li?u, don vi ph?n tram */
    unsigned corrupt = 0;        /* T? l? l?m h?ng frame D? li?u */
    int fd;                      /* T?p b? m? t? socket client */

    /* =====================================================================
     * Step 1: Kiem tra tham so dau vao
     * =====================================================================
     */
    if (argc < 3 || argc > 5 ||
        parse_port(argv[2], &port) < 0 ||
        (argc > 3 && parse_percent(argv[3], &loss) < 0) ||
        (argc > 4 && parse_percent(argv[4], &corrupt) < 0) ||
        inet_pton(AF_INET, argv[1], &address.sin_addr) != 1) {
        fprintf(stderr, "Usage: %s <IPAddress> <Port> [Loss%%] [Corrupt%%]\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    /* Khoi tao seed ngau nhi?n cho bo mo phong mat/hong frame. */
    srand((unsigned)(getpid() ^ (unsigned)now_ms()));
    signal(SIGPIPE, SIG_IGN);

    /* =====================================================================
     * Step 2: Tao socket TCP IPv4
     * =====================================================================
     */
    /*
     * Hàm socket():
     *   - AF_INET: sử dụng địa chỉ IPv4.
     *   - SOCK_STREAM: sử dụng TCP, có kết nối và đảm bảo thứ tự byte.
     *   - 0: chọn giao thức mặc định tương ứng với SOCK_STREAM là TCP.
     *   - Giá trị trả về: socket descriptor nếu thành công, -1 nếu lỗi.
     */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        /* perror() in thông báo lỗi dựa trên giá trị errno của system call. */
        perror("socket");
        return EXIT_FAILURE;
    }
    {
        int no_delay = 1; /* Tắt Nagle để ACK 16 byte được gửi ngay */
        /*
         * setsockopt() bật TCP_NODELAY trên socket client.
         *   - fd: socket TCP vừa tạo.
         *   - IPPROTO_TCP/TCP_NODELAY: gửi ngay gói nhỏ, không chờ gom frame.
         *   - no_delay = 1: bật tùy chọn.
         *   - Trả về 0 nếu thành công, -1 nếu lỗi.
         */
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                       &no_delay, sizeof(no_delay)) < 0) {
            perror("setsockopt(TCP_NODELAY)");
            close(fd);
            return EXIT_FAILURE;
        }
    }

    /* =====================================================================
     * Step 3: Thiet lap dia chi va ket noi den server
     * =====================================================================
     */
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;              /* Họ địa chỉ IPv4 */
    address.sin_port = htons((uint16_t)port);  /* Đổi cung sang network order */
    /*
     * Hàm inet_pton():
     *   - AF_INET: địa chỉ cần chuyển là IPv4.
     *   - argv[1]: chuỗi địa chỉ IPv4 dạng "127.0.0.1".
     *   - &address.sin_addr: nơi ghi địa chỉ nhị phân.
     *   - Giá trị trả về: 1 nếu hợp lệ, 0 nếu sai định dạng, -1 nếu lỗi.
     */
    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1 ||
        /*
         * Hàm connect():
         *   - fd: socket TCP của client.
         *   - address: địa chỉ server đích.
         *   - sizeof(address): kích thước cấu trúc địa chỉ.
         *   - Giá trị trả về: 0 nếu kết nối thành công, -1 nếu lỗi.
         */
        connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        /* perror() cho biết lỗi thường gặp như server chưa listen hoặc sai port. */
        perror("connect");
        close(fd);
        return EXIT_FAILURE;
    }

    /* =====================================================================
     * Step 4: Vong lap menu va xu ly request
     * =====================================================================
     */
    for (;;) {
        char line[2048]; /* B? ??m nh?p l?a ch?n, chu?i v? ???ng d?n t?p */
        printf("MENU\n-----------------------------------\n"
               "1. Send any string\n2. Send file content\n3. Exit\n"
               "Your choice: ");
        fflush(stdout);
        /* fgets() doc mot dong tu ban phim; NULL la EOF hoac loi nhap. */
        if (fgets(line, sizeof(line), stdin) == NULL || line[0] == '3') {
            break;
        }
        if (line[0] == '1') {
            printf("Enter string: ");
            fflush(stdout);
            if (fgets(line, sizeof(line), stdin) == NULL) {
                break;
            }
            line[strcspn(line, "\n")] = '\0';
            if (strlen(line) > MAX_STR) {
                printf("Error: String too long (max %d characters)\n",
                       MAX_STR);
                continue;
            }
            if (line[0] != '\0' && send_string(fd, line) < 0) {
                break;
            }
        } else if (line[0] == '2') {
            printf("Enter file name: ");
            fflush(stdout);
            if (fgets(line, sizeof(line), stdin) == NULL) {
                break;
            }
            line[strcspn(line, "\n")] = '\0';
            if (line[0] != '\0' && send_file(fd, line, loss, corrupt) < 0) {
                break;
            }
        }
    }
    /* =====================================================================
     * Step 5: Dong socket va ket thuc client
     * =====================================================================
     */
    /*
     * Hàm open():
     *   - fd: socket descriptor cần đóng.
     *   - Đóng kết nối TCP và giải phóng tài nguyên kernel.
     */
    close(fd);
    printf("Connection closed. Goodbye!\n");
    return EXIT_SUCCESS;
}
