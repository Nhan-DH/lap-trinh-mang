/* Server TCP HW7: xử lý chuỗi và truyền tệp tin cậy. */
#define _GNU_SOURCE
#include "proto.h"          /* Hiệp ước frame và các tiện ích dùng chung. */

#include <arpa/inet.h>      /* Thư viện địa chỉ IPv4. */
#include <ctype.h>          /* Kiểm tra ký tự chữ và số. */
#include <errno.h>          /* Mã lỗi hệ thống. */
#include <netinet/in.h>     /* Cấu trúc địa chỉ IPv4. */
#include <netinet/tcp.h>    /* Tối ưu frame ACK ngắn bằng TCP_NODELAY. */
#include <poll.h>           /* Chờ dữ liệu trên socket. */
#include <signal.h>         /* Tín hiệu tiến trình. */
#include <stdio.h>          /* Nhập xuất chuẩn. */
#include <stdlib.h>         /* Cấp phát bộ nhớ và chuyển đổi số. */
#include <string.h>         /* Xử lý chuỗi. */
#include <sys/socket.h>     /* Gọi hệ thống socket. */
#include <sys/stat.h>       /* Thông tin tệp và thư mục. */
#include <sys/types.h>      /* Kiểu dữ liệu hệ thống. */
#include <sys/wait.h>       /* Thu dọn tiến trình con. */
#include <unistd.h>         /* Các lời gọi hệ thống POSIX. */

typedef struct {
    /* Một ô của bộ đệm vòng, có thể chứa chunk sai thứ tự. */
    uint8_t *data;
    size_t len;
    uint32_t seq; /* Trạng thái của một tệp đang được nhận. */
    int present;
} slot_t;

typedef struct {
    /* Các bộ đếm thống kê của giao dịch. */
    int fd;
    uint64_t size;
    uint32_t total;
    uint32_t expected;
    uint32_t next_write;
    unsigned buffer_count;
    unsigned drain_ms;
    unsigned ack_loss;
    slot_t *slots;
    FILE *file;
    md5_t *md5;
    char path[512];
    uint32_t received_chunks; /* Phân tích tham số trong khoảng cho phép. */
    uint32_t duplicate_chunks; /* Kiểm tra chuỗi chỉ gồm chữ và số ASCII. */
    uint32_t out_of_order; /* Tính MD5 chuỗi và gửi kết quả. */
    uint32_t bad_crc; /* Kiểm tra tên tệp an toàn, không chứa path traversal. */
    uint32_t outside_window; /* Giải phóng trạng thái truyền tệp. */
    uint32_t buffered_chunks; /* Khởi tạo tệp đích và bộ đệm vòng. */
    uint32_t peak_buffered; /* Gửi ACK tích lũy và cửa sổ còn tr?ng. */
    uint32_t acks_sent; /* Ghi các chunk đã đủ và cập nhật MD5. */
    uint32_t acks_dropped; /* Tiếp nhận DATA, lưu vào bộ đệm và phát ACK. */
    uint32_t zero_window_advertised; /* Kiểm tra tệp đã nhận đủ và gửi FMD5. */
} transfer_t;

/* Xử lý toàn bộ frame của một client. */
static void reap_children(int signal_number)
{
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

/* Phân tích tham số d?ng lệnh. */
static int parse_unsigned(const char *text, unsigned min, unsigned max,
                          unsigned *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' ||
        parsed < min || parsed > max) {
        return -1;
    }
    *value = (unsigned)parsed;
    return 0;
}

/* Tạo socket, bind, listen và chấp nhận client. */
static int valid_string(const uint8_t *data, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        if (!isalnum(data[i])) {
            return 0;
        }
    }
    return 1;
}

/* Server TCP HW7: xử lý chuỗi và truyền tệp tin cậy. */
static int send_string_reply(int fd, const uint8_t *data, size_t len,
                             const struct sockaddr_in *peer)
{
    char md5[33];
    char letters[33];
    char digits[33];
    char peer_ip[INET_ADDRSTRLEN];

    if (!valid_string(data, len)) {
        static const char error[] =
            "Error: String contains non-alphanumeric characters";
        if (inet_ntop(AF_INET, &peer->sin_addr, peer_ip,
                      sizeof(peer_ip)) == NULL) {
            snprintf(peer_ip, sizeof(peer_ip), "unknown");
        }
        printf("[%s:%u] Received string: \"%.*s\"\n"
               "-> %s\n"
               "Error message sent.\n",
               peer_ip, (unsigned)ntohs(peer->sin_port), (int)len, data,
               error);
        fflush(stdout);
        return send_frame(fd, T_ERR, 0, 0, error, sizeof(error) - 1);
    }
    md5_buf_hex(data, len, md5);
    split_md5(md5, letters, digits);
    if (inet_ntop(AF_INET, &peer->sin_addr, peer_ip,
                  sizeof(peer_ip)) == NULL) {
        snprintf(peer_ip, sizeof(peer_ip), "unknown");
    }
    printf("[%s:%u] Received string: \"%.*s\"\n"
           "MD5 : %s\n"
           "Letters : %s\n"
           "Digits  : %s\n",
           peer_ip, (unsigned)ntohs(peer->sin_port), (int)len, data, md5,
           letters, digits);
    fflush(stdout);
    if (send_frame(fd, T_LETTERS, 0, 0, letters,
                   (uint32_t)strlen(letters)) < 0) {
        return -1;
    }
    return send_frame(fd, T_DIGITS, 0, 0, digits,
                      (uint32_t)strlen(digits));
}

/* Hiệp ước frame và các tiện ích dùng chung. */
static int valid_name(const uint8_t *name, size_t len)
{
    size_t i;
    if (len == 0 || len > MAX_NAME ||
        (len == 1 && name[0] == '.') ||
        (len == 2 && name[0] == '.' && name[1] == '.')) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == '\0') {
            return 0;
        }
    }
    return 1;
}

/* Thư viện địa chỉ IPv4. */
static void free_transfer(transfer_t *transfer, int remove_partial)
{
    unsigned i;
    if (transfer->md5 != NULL) {
        char ignored[33];
        md5_final_hex(transfer->md5, ignored);
        transfer->md5 = NULL;
    }
    if (transfer->file != NULL) {
        fclose(transfer->file);
        transfer->file = NULL;
    }
    if (remove_partial && transfer->path[0] != '\0') {
        unlink(transfer->path);
    }
    for (i = 0; transfer->slots != NULL && i < transfer->buffer_count; ++i) {
        free(transfer->slots[i].data);
    }
    free(transfer->slots);
    transfer->slots = NULL;
}

/* Kiểm tra ký tự chữ và số. */
static int init_transfer(transfer_t *transfer, int fd, unsigned buffer_count,
                         unsigned drain_ms, unsigned ack_loss,
                         uint64_t size, const uint8_t *name, size_t name_len,
                         const struct sockaddr_in *peer)
{
    char ip[INET_ADDRSTRLEN];
    unsigned i;

    if (inet_ntop(AF_INET, &peer->sin_addr, ip, sizeof(ip)) == NULL) {
        return -1;
    }
    if (mkdir("received_files", 0775) < 0 && errno != EEXIST) {
        return -1;
    }
    transfer->fd = fd;
    transfer->size = size;
    transfer->total = size == 0 ? 0 :
        (uint32_t)((size + CHUNK_SIZE - 1) / CHUNK_SIZE);
    transfer->buffer_count = buffer_count;
    transfer->drain_ms = drain_ms;
    transfer->ack_loss = ack_loss;
    transfer->slots = calloc(buffer_count, sizeof(*transfer->slots));
    if (transfer->slots == NULL) {
        return -1;
    }
    if (snprintf(transfer->path, sizeof(transfer->path),
                 "received_files/%s_%u_%.*s", ip,
                 (unsigned)ntohs(peer->sin_port), (int)name_len, name) >=
        (int)sizeof(transfer->path)) {
        return -1;
    }
    transfer->file = fopen(transfer->path, "wb");
    if (transfer->file == NULL) {
        return -1;
    }
    transfer->md5 = md5_new();
    if (transfer->md5 == NULL) {
        return -1;
    }
    for (i = 0; i < buffer_count; ++i) {
        transfer->slots[i].data = malloc(CHUNK_SIZE);
        if (transfer->slots[i].data == NULL) {
            return -1;
        }
    }
    printf("Receiving file: \"%.*s\" (%llu bytes, %u chunks, buffer %u)\n",
           (int)name_len, name, (unsigned long long)size, transfer->total,
           buffer_count);
    fflush(stdout);
    return 0;
}

/* Mã lỗi hệ thống. */
static int send_ack(transfer_t *transfer)
{
    uint32_t available = transfer->next_write + transfer->buffer_count -
                         transfer->expected;
    uint16_t window = available > UINT16_MAX ? UINT16_MAX :
                      (uint16_t)available;
    if (chance((double)transfer->ack_loss)) {
        transfer->acks_dropped++;
        return 0;
    }
    if (window == 0) {
        transfer->zero_window_advertised++;
    }
    if (send_frame(transfer->fd, T_ACK, window, transfer->expected,
                   NULL, 0) < 0) {
        return -1;
    }
    transfer->acks_sent++;
    return 0;
}

/* Cấu trúc địa chỉ IPv4. */
static int drain_slots(transfer_t *transfer)
{
    while (transfer->next_write < transfer->expected) {
        slot_t *slot = &transfer->slots[transfer->next_write %
                                         transfer->buffer_count];
        if (!slot->present || slot->seq != transfer->next_write) {
            break;
        }
        if (transfer->drain_ms > 0) {
            usleep((useconds_t)transfer->drain_ms * 1000);
        }
        if (fwrite(slot->data, 1, slot->len, transfer->file) != slot->len) {
            return -1;
        }
        md5_update(transfer->md5, slot->data, slot->len);
        slot->present = 0;
        transfer->buffered_chunks--;
        transfer->next_write++;
    }
    return 0;
}

/* Tối ưu frame ACK ngắn bằng TCP_NODELAY. */
static int receive_file(transfer_t *transfer, const frame_hdr_t *header,
                        const uint8_t *payload)
{
    uint32_t seq;
    size_t data_len;
    slot_t *slot;
    int result;

    if (header->len < sizeof(uint32_t)) {
        return 0;
    }
    seq = get32(payload);
    data_len = header->len - sizeof(uint32_t);
    if (seq >= transfer->total || data_len == 0 || data_len > CHUNK_SIZE) {
        transfer->outside_window++;
        return send_ack(transfer);
    }
    if (seq < transfer->expected) {
        transfer->duplicate_chunks++;
        return send_ack(transfer);
    }
    if (seq >= transfer->next_write + transfer->buffer_count) {
        transfer->outside_window++;
        return send_ack(transfer);
    }
    if (seq > transfer->expected) {
        transfer->out_of_order++;
    }
    slot = &transfer->slots[seq % transfer->buffer_count];
    if (slot->present && slot->seq == seq) {
        transfer->duplicate_chunks++;
        return send_ack(transfer);
    }
    if (slot->present) {
        transfer->outside_window++;
        return send_ack(transfer);
    }
    memcpy(slot->data, payload + sizeof(uint32_t), data_len);
    slot->len = data_len;
    slot->seq = seq;
    slot->present = 1;
    transfer->received_chunks++;
    transfer->buffered_chunks++;
    if (transfer->buffered_chunks > transfer->peak_buffered) {
        transfer->peak_buffered = transfer->buffered_chunks;
    }
    while (transfer->expected < transfer->total &&
           transfer->slots[transfer->expected %
                          transfer->buffer_count].present &&
           transfer->slots[transfer->expected %
                          transfer->buffer_count].seq == transfer->expected) {
        transfer->expected++;
    }
    result = send_ack(transfer);
    if (result < 0) {
        return result;
    }
    if (transfer->buffered_chunks >= transfer->buffer_count) {
        if (drain_slots(transfer) < 0) {
            return -1;
        }
        return send_ack(transfer);
    }
    return 0;
}

/* Chờ dữ liệu trên socket. */
static int finish_transfer(transfer_t *transfer, uint32_t total,
                           char md5[33])
{
    if (drain_slots(transfer) < 0) {
        return -1;
    }
    if (total != transfer->total || transfer->expected != transfer->total ||
        transfer->next_write != transfer->total ||
        fflush(transfer->file) != 0) {
        fprintf(stderr,
                "Error: incomplete transfer at FEND "
                "(reported=%u, expected total=%u, next=%u, written=%u).\n",
                total, transfer->total, transfer->expected,
                transfer->next_write);
        return -1;
    }
    md5_final_hex(transfer->md5, md5);
    transfer->md5 = NULL;
    return send_frame(transfer->fd, T_FMD5, 0, total, md5, 32);
}

/* Tín hiệu tiến trình. */
static void handle_client(int fd, const struct sockaddr_in *peer,
                          unsigned buffer_count, unsigned drain_ms,
                          unsigned ack_loss)
{
    char peer_ip[INET_ADDRSTRLEN]; /* Nhập xuất chuẩn. */
    uint8_t payload[MAX_PAYLOAD]; /* Cấp phát bộ nhớ và chuyển đổi số. */
    frame_hdr_t header;            /* Xử lý chuỗi. */
    transfer_t transfer;           /* Gọi hệ thống socket. */
    int active = 0;                /* Thông tin tệp và thư mục. */

    memset(&transfer, 0, sizeof(transfer));
    transfer.fd = fd;
    if (inet_ntop(AF_INET, &peer->sin_addr, peer_ip, sizeof(peer_ip)) == NULL) {
        snprintf(peer_ip, sizeof(peer_ip), "unknown");
    }
    /* Kiểu dữ liệu hệ thống. */
    for (;;) {
        struct pollfd descriptor = { fd, POLLIN, 0 };
        int ready = poll(&descriptor, 1, -1);
        int result;

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            fprintf(stderr, "Transfer aborted: client idle timeout.\n");
            break;
        }
        /* Thu dọn tiến trình con. */
        result = recv_frame(fd, &header, payload);
        if (result == RF_EOF || result == RF_ERR) {
            break;
        }
        if (result == RF_CRC || result == RF_PROTO) {
            if (active && result == RF_CRC) {
                transfer.bad_crc++;
            }
            continue;
        }
        /* Các lời gọi hệ thống POSIX. */
        if (!active && header.type == T_STR) {
            if (header.len > MAX_STR) {
                static const char error[] =
                    "Error: String too long (max 1024 characters)";
                char peer_ip[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &peer->sin_addr, peer_ip,
                              sizeof(peer_ip)) == NULL) {
                    snprintf(peer_ip, sizeof(peer_ip), "unknown");
                }
                printf("[%s:%u] String rejected: length %u exceeds %d.\n",
                       peer_ip, (unsigned)ntohs(peer->sin_port), header.len,
                       MAX_STR);
                fflush(stdout);
                if (send_frame(fd, T_ERR, 0, 0, error,
                               sizeof(error) - 1) < 0) {
                    break;
                }
                continue;
            }
            if (send_string_reply(fd, payload, header.len, peer) < 0) {
                break;
            }
        /* Một ô của bộ đệm vòng, có thể chứa chunk sai thứ tự. */
        } else if (!active && header.type == T_FBEGIN &&
                   header.len > sizeof(uint64_t)) {
            uint64_t size = get64(payload);
            size_t name_len = header.len - sizeof(uint64_t);
            if (!valid_name(payload + sizeof(uint64_t), name_len) ||
                init_transfer(&transfer, fd, buffer_count, drain_ms,
                              ack_loss, size, payload + sizeof(uint64_t),
                              name_len, peer) < 0) {
                static const char error[] = "Error: Cannot create file";
                send_frame(fd, T_ERR, 0, 0, error, sizeof(error) - 1);
                free_transfer(&transfer, 1);
                memset(&transfer, 0, sizeof(transfer));
                transfer.fd = fd;
                continue;
            }
            active = 1;
            if (send_ack(&transfer) < 0) {
                break;
            }
        /* Trạng thái của một tệp đang được nhận. */
        } else if (active && header.type == T_DATA) {
            if (receive_file(&transfer, &header, payload) < 0) {
                break;
            }
        /* Các bộ đếm thống kê của giao dịch. */
        } else if (active && header.type == T_PROBE) {
            if (send_ack(&transfer) < 0) {
                break;
            }
        /* Phân tích tham số trong khoảng cho phép. */
        } else if (active && header.type == T_FEND) {
            char md5[33];
            if (finish_transfer(&transfer, header.seq, md5) < 0) {
                break;
            }
            printf("Saved at: %s\nFile size: %llu bytes\nMD5: %s\n",
                   transfer.path, (unsigned long long)transfer.size, md5);
            if (transfer.size <= 65536) {
                FILE *preview = fopen(transfer.path, "rb");
                if (preview != NULL) {
                    unsigned char text[65536];
                    size_t n = fread(text, 1, sizeof(text), preview);
                    int printable = 1;
                    size_t i;
                    for (i = 0; i < n; ++i) {
                        if (text[i] != '\n' && text[i] != '\r' &&
                            text[i] != '\t' &&
                            (text[i] < 32 || text[i] > 126)) {
                            printable = 0;
                            break;
                        }
                    }
                    if (printable && n > 0) {
                        printf("Content:\n");
                        fwrite(text, 1, n, stdout);
                        if (n == 0 || text[n - 1] != '\n') {
                            putchar('\n');
                        }
                    }
                    fclose(preview);
                }
            }
            printf("Chunks     : %u total, %u duplicate, %u out-of-order, "
                   "%u bad CRC, %u outside window\n",
                   transfer.total, transfer.duplicate_chunks,
                   transfer.out_of_order, transfer.bad_crc,
                   transfer.outside_window);
            printf("Buffer     : %u chunks, peak %u, zero window advertised %u times\n"
                   "ACKs       : %u sent, %u dropped (simulated)\n",
                   transfer.buffer_count, transfer.peak_buffered,
                   transfer.zero_window_advertised, transfer.acks_sent,
                   transfer.acks_dropped);
            fflush(stdout);
            free_transfer(&transfer, 0);
            memset(&transfer, 0, sizeof(transfer));
            transfer.fd = fd;
            active = 0;
        }
    }
    /* Kiểm tra chuỗi chỉ gồm chữ và số ASCII. */
    if (active) {
        free_transfer(&transfer, 1);
    } else {
        free_transfer(&transfer, 0);
    }
    printf("[%s:%u] Client disconnected.\n", peer_ip,
           (unsigned)ntohs(peer->sin_port));
    fflush(stdout);
    close(fd);
}

/* Tính MD5 chuỗi và gửi kết quả. */
int main(int argc, char **argv)
{
    struct sockaddr_in address; /* Kiểm tra tên tệp an toàn, không chứa path traversal. */
    struct sigaction action;    /* Giải phóng trạng thái truyền tệp. */
    unsigned port;              /* Khởi tạo tệp đích và bộ đệm vòng. */
    unsigned buffer_count = DEFAULT_BUF; /* Gửi ACK tích lũy và cửa sổ còn tr?ng. */
    unsigned drain_ms = 0;      /* Ghi các chunk đã đủ và cập nhật MD5. */
    unsigned ack_loss = 0;      /* Tiếp nhận DATA, lưu vào bộ đệm và phát ACK. */
    int server_fd;              /* Kiểm tra tệp đã nhận đủ và gửi FMD5. */

    /* Xử lý toàn bộ frame của một client. */
    if (argc < 2 || argc > 5 ||
        parse_unsigned(argv[1], 1, 65535, &port) < 0 ||
        (argc > 2 && parse_unsigned(argv[2], 1, UINT16_MAX, &buffer_count) < 0) ||
        (argc > 3 && parse_unsigned(argv[3], 0, 60000, &drain_ms) < 0) ||
        (argc > 4 && parse_unsigned(argv[4], 0, 100, &ack_loss) < 0)) {
        fprintf(stderr, "Usage: %s <Port> [BufferChunks] [DrainMs] [AckLoss%%]\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = reap_children;
    sigemptyset(&action.sa_mask);
    sigaction(SIGCHLD, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* Phân tích tham số d?ng lệnh. */
    /* Tạo socket, bind, listen và chấp nhận client. */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        /* Server TCP HW7: xử lý chuỗi và truyền tệp tin cậy. */
        perror("socket");
        return EXIT_FAILURE;
    }
    {
        int reuse = 1; /* Hiệp ước frame và các tiện ích dùng chung. */
        /* Thư viện địa chỉ IPv4. */
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }

    /* Kiểm tra ký tự chữ và số. */
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;             /* Mã lỗi hệ thống. */
    address.sin_addr.s_addr = htonl(INADDR_ANY); /* Cấu trúc địa chỉ IPv4. */
    address.sin_port = htons((uint16_t)port); /* Tối ưu frame ACK ngắn bằng TCP_NODELAY. */
    /* Chờ dữ liệu trên socket. */
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }
    /* Tín hiệu tiến trình. */
    if (listen(server_fd, 16) < 0) {
        perror("bind/listen");
        close(server_fd);
        return EXIT_FAILURE;
    }
    printf("TCP server started at port %u. Waiting for clients...\n", port);

    /* Nhập xuất chuẩn. */
    for (;;) {
        struct sockaddr_in peer; /* Cấp phát bộ nhớ và chuyển đổi số. */
        socklen_t peer_len = sizeof(peer); /* Xử lý chuỗi. */
        /* Gọi hệ thống socket. */
        int client_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        {
            int no_delay = 1; /* Thông tin tệp và thư mục. */
            /* Kiểu dữ liệu hệ thống. */
            if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                           &no_delay, sizeof(no_delay)) < 0) {
                perror("setsockopt(TCP_NODELAY)");
                close(client_fd);
                continue;
            }
        }
        {
            char peer_ip[INET_ADDRSTRLEN]; /* Thu dọn tiến trình con. */
            if (inet_ntop(AF_INET, &peer.sin_addr, peer_ip,
                          sizeof(peer_ip)) == NULL) {
                snprintf(peer_ip, sizeof(peer_ip), "unknown");
            }
            printf("[%s:%u] Client connected.\n", peer_ip,
                   (unsigned)ntohs(peer.sin_port));
            fflush(stdout);
        }
        /* Các lời gọi hệ thống POSIX. */
        {
            pid_t child_pid = fork();
            if (child_pid == 0) {
            /* Một ô của bộ đệm vòng, có thể chứa chunk sai thứ tự. */
            close(server_fd);
            handle_client(client_fd, &peer, buffer_count, drain_ms, ack_loss);
            _exit(EXIT_SUCCESS);
            }
            if (child_pid < 0) {
                perror("fork");
            }
        }
        /* Trạng thái của một tệp đang được nhận. */
        close(client_fd);
    }
    /* Các bộ đếm thống kê của giao dịch. */
    close(server_fd);
    return EXIT_FAILURE;
}
