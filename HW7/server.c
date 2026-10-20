/*
 * server.c - HW7 TCP server.
 *
 * Server fork mot tien trinh cho moi client, xu ly chuoi va nhan file lon
 * bang buffer vong. Moi chunk duoc kiem tra CRC o tang frame, xac nhan bang
 * ACK tich luy va ghi xuong dia theo dung thu tu.
 */
#define _GNU_SOURCE
#include "proto.h"          /* Hieu uoc frame va ham MD5/CRC dung chung */

#include <arpa/inet.h>      /* inet_ntop va htons/ntohs cho IPv4 */
#include <ctype.h>          /* isalnum kiem tra chuoi hop le */
#include <errno.h>          /* errno, EINTR va EEXIST */
#include <netinet/in.h>     /* struct sockaddr_in va INADDR_ANY */
#include <poll.h>           /* poll tao idle timeout cho client */
#include <signal.h>         /* signal, SIGCHLD, SIGPIPE */
#include <stdio.h>          /* printf, fprintf, fopen, fwrite, perror */
#include <stdlib.h>         /* strtoul, calloc, malloc va EXIT_* */
#include <string.h>         /* memset, memcpy, strlen, snprintf */
#include <sys/socket.h>     /* socket, setsockopt, bind, listen, accept */
#include <sys/stat.h>       /* mkdir tao thu muc received_files */
#include <sys/types.h>      /* kieu pid_t va socklen_t */
#include <sys/wait.h>       /* waitpid thu don tien trinh con */
#include <unistd.h>         /* close, fork, unlink, usleep */

typedef struct {
    /* Mot o trong buffer vong, co the chua chunk den sai thu tu. */
    uint8_t *data;
    size_t len;
    int present;
} slot_t;

typedef struct {
    /* Trang thai cua mot file dang duoc nhan tu client. */
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
} transfer_t;

/**
 * @brief Thu don cac tien trinh con da ket thuc sau fork().
 * @param[in] signal_number Ma tin hieu SIGCHLD, khong dung trong ham.
 */
static void reap_children(int signal_number)
{
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

/**
 * @brief Phan tich so nguyen trong mot khoang gioi han.
 * @param[in] text Chuoi so can phan tich.
 * @param[in] min Gia tri nho nhat duoc chap nhan.
 * @param[in] max Gia tri lon nhat duoc chap nhan.
 * @param[out] value Noi nhan gia tri da chuyen doi.
 * @return 0 neu hop le, -1 neu chuoi hoac khoang khong hop le.
 */
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

/**
 * @brief Kiem tra chuoi chi gom ky tu ASCII chu va so.
 * @param[in] data Vung byte chuoi can kiem tra.
 * @param[in] len So byte can kiem tra.
 * @return 1 neu hop le, 0 neu co ky tu khong phai chu hoac so.
 */
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

/**
 * @brief Tinh MD5 chuoi, tach chu/so va gui hai frame ket qua.
 * @param[in] fd Socket TCP cua client.
 * @param[in] data Chuoi dau vao dang byte.
 * @param[in] len So byte chuoi.
 * @return 0 neu gui thanh cong, -1 neu socket loi.
 */
static int send_string_reply(int fd, const uint8_t *data, size_t len)
{
    char md5[33];
    char letters[33];
    char digits[33];

    if (!valid_string(data, len)) {
        static const char error[] =
            "Error: String contains non-alphanumeric characters";
        return send_frame(fd, T_ERR, 0, 0, error, sizeof(error) - 1);
    }
    md5_buf_hex(data, len, md5);
    split_md5(md5, letters, digits);
    if (send_frame(fd, T_LETTERS, 0, 0, letters,
                   (uint32_t)strlen(letters)) < 0) {
        return -1;
    }
    return send_frame(fd, T_DIGITS, 0, 0, digits,
                      (uint32_t)strlen(digits));
}

/**
 * @brief Kiem tra ten file khong chua path traversal hoac byte cam.
 * @param[in] name Ten file khong co ky tu ket thuc '\0' bat buoc.
 * @param[in] len So byte cua ten file.
 * @return 1 neu ten hop le, 0 neu vi pham quy tac ten file.
 */
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

/**
 * @brief Giai phong trang thai truyen file.
 * @param[in,out] transfer Trang thai file can giai phong.
 * @param[in] remove_partial 1 de xoa file dang nhan, 0 de giu file hoan tat.
 */
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

/**
 * @brief Tao file dich va cap phat buffer vong cho mot giao dich.
 * @param[out] transfer Noi luu trang thai giao dich moi.
 * @param[in] fd Socket TCP cua client.
 * @param[in] buffer_count So o buffer vong.
 * @param[in] drain_ms Do tre ghi moi chunk, tinh bang mili-giay.
 * @param[in] ack_loss Ty le bo ACK mo phong.
 * @param[in] size Kich thuoc file.
 * @param[in] name Ten file goc.
 * @param[in] name_len So byte cua ten file.
 * @param[in] peer Dia chi IPv4 va port cua client.
 * @return 0 neu khoi tao thanh cong, -1 neu loi.
 */
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
    return 0;
}

/**
 * @brief Gui ACK tich luy va cua so con trong, co the mo phong mat ACK.
 * @param[in,out] transfer Trang thai giao dich can ACK.
 * @return 0 neu gui thanh cong hoac ACK bi mo phong bo, -1 neu send loi.
 */
static int send_ack(transfer_t *transfer)
{
    uint32_t available = transfer->next_write + transfer->buffer_count -
                         transfer->expected;
    uint16_t window = available > UINT16_MAX ? UINT16_MAX :
                      (uint16_t)available;
    if (chance((double)transfer->ack_loss)) {
        return 0;
    }
    return send_frame(transfer->fd, T_ACK, window, transfer->expected, NULL, 0);
}

/**
 * @brief Ghi lien tuc cac chunk da du va cap nhat MD5.
 * @param[in,out] transfer Trang thai giao dich va buffer vong.
 * @return 0 neu ghi thanh cong, -1 neu loi ghi file.
 */
static int drain_slots(transfer_t *transfer)
{
    while (transfer->next_write < transfer->expected) {
        slot_t *slot = &transfer->slots[transfer->next_write %
                                         transfer->buffer_count];
        if (!slot->present) {
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
        transfer->next_write++;
    }
    return 0;
}

/**
 * @brief Tiep nhan mot frame DATA, luu vao buffer va phat ACK.
 * @param[in,out] transfer Trang thai file dang nhan.
 * @param[in] header Header cua frame DATA da kiem tra CRC.
 * @param[in] payload Payload gom seq 4 byte va du lieu chunk.
 * Chunk den sai thu tu duoc giu lai cho toi khi co du khoang trong.
 * @return 0 neu xu ly thanh cong, -1 neu ghi hoac gui ACK gap loi.
 */
static int receive_file(transfer_t *transfer, const frame_hdr_t *header,
                        const uint8_t *payload)
{
    uint32_t seq;
    size_t data_len;
    slot_t *slot;

    if (header->len < sizeof(uint32_t)) {
        return 0;
    }
    seq = get32(payload);
    data_len = header->len - sizeof(uint32_t);
    if (seq >= transfer->total ||
        data_len == 0 || data_len > CHUNK_SIZE ||
        seq < transfer->expected ||
        seq >= transfer->next_write + transfer->buffer_count) {
        return send_ack(transfer);
    }
    slot = &transfer->slots[seq % transfer->buffer_count];
    if (!slot->present) {
        memcpy(slot->data, payload + sizeof(uint32_t), data_len);
        slot->len = data_len;
        slot->present = 1;
        while (transfer->expected < transfer->total &&
               transfer->slots[transfer->expected %
                              transfer->buffer_count].present) {
            transfer->expected++;
        }
    }
    if (drain_slots(transfer) < 0) {
        return -1;
    }
    return send_ack(transfer);
}

/**
 * @brief Kiem tra file da nhan du, ket thuc MD5 va gui FMD5.
 * @param[in,out] transfer Trang thai giao dich da nhan file.
 * @param[in] total Tong so chunk do client bao cao trong FEND.
 * @param[out] md5 Bo dem nhan MD5 32 ky tu hex in hoa.
 * @return 0 neu file hop le va FMD5 gui thanh cong, -1 neu loi.
 */
static int finish_transfer(transfer_t *transfer, uint32_t total,
                           char md5[33])
{
    if (total != transfer->total || transfer->expected != transfer->total ||
        transfer->next_write != transfer->total ||
        fflush(transfer->file) != 0) {
        return -1;
    }
    md5_final_hex(transfer->md5, md5);
    transfer->md5 = NULL;
    return send_frame(transfer->fd, T_FMD5, 0, total, md5, 32);
}

/**
 * @brief Vong lap xu ly mot client cho toi khi dong ket noi/timeout.
 *
 * @param[in] fd Socket client duoc tao boi accept().
 * @param[in] peer Dia chi IPv4 va port cua client.
 * @param[in] buffer_count So o buffer vong cho file.
 * @param[in] drain_ms Do tre ghi moi chunk.
 * @param[in] ack_loss Ty le bo ACK mo phong.
 * Moi giao dich duoc xu ly trong mot frame loop: T_STR, T_FBEGIN, T_DATA,
 * T_PROBE va T_FEND. File dang nhan bi xoa neu phien bi huy.
 */
static void handle_client(int fd, const struct sockaddr_in *peer,
                          unsigned buffer_count, unsigned drain_ms,
                          unsigned ack_loss)
{
    uint8_t payload[MAX_PAYLOAD]; /* Payload cua frame dang nhan */
    frame_hdr_t header;            /* Header frame sau khi giai ma */
    transfer_t transfer;           /* Trang thai file dang nhan */
    int active = 0;                /* 1 khi dang co giao dich FBEGIN/FEND */

    memset(&transfer, 0, sizeof(transfer));
    transfer.fd = fd;
    /* =====================================================================
     * Step 1: Cho frame voi idle timeout
     * =====================================================================
     */
    for (;;) {
        struct pollfd descriptor = { fd, POLLIN, 0 };
        int ready = poll(&descriptor, 1, IDLE_MS);
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
        /*
         * Hàm recv_frame():
         *   - Đọc đủ header và payload từ socket TCP.
         *   - Kiểm tra độ dài và CRC32 trước khi trả dữ liệu cho tầng ứng dụng.
         *   - RF_OK là frame hợp lệ; RF_CRC/RF_PROTO là frame lỗi.
         */
        result = recv_frame(fd, &header, payload);
        if (result == RF_EOF || result == RF_ERR) {
            break;
        }
        if (result == RF_CRC || result == RF_PROTO) {
            continue;
        }
        /* Step 2: Xu ly frame T_STR khi chua co file dang nhan. */
        if (!active && header.type == T_STR) {
            if (header.len <= MAX_STR &&
                send_string_reply(fd, payload, header.len) < 0) {
                break;
            }
        /* Step 3: Khoi tao giao dich file sau khi nhan FBEGIN. */
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
        /* Step 4: Nhan DATA, luu buffer va ghi chunk theo thu tu. */
        } else if (active && header.type == T_DATA) {
            if (receive_file(&transfer, &header, payload) < 0) {
                break;
            }
        /* Step 5: Tra ACK hien tai khi client gui PROBE. */
        } else if (active && header.type == T_PROBE) {
            if (send_ack(&transfer) < 0) {
                break;
            }
        /* Step 6: Ket thuc file, tinh MD5 va gui FMD5. */
        } else if (active && header.type == T_FEND) {
            char md5[33];
            if (finish_transfer(&transfer, header.seq, md5) < 0) {
                break;
            }
            printf("Saved at: %s\nFile size: %llu bytes\nMD5: %s\n",
                   transfer.path, (unsigned long long)transfer.size, md5);
            free_transfer(&transfer, 0);
            memset(&transfer, 0, sizeof(transfer));
            transfer.fd = fd;
            active = 0;
        }
    }
    /* Step 7: Dong file, giai phong buffer va xoa file neu dang dang do. */
    if (active) {
        free_transfer(&transfer, 1);
    } else {
        free_transfer(&transfer, 0);
    }
    close(fd);
}

/**
 * @brief Khoi tao TCP server, lang nghe va fork xu ly client.
 *
 * Chi tiet luong xu ly:
 *   - Buoc 1: Kiem tra Port, BufferChunks, DrainMs va AckLoss%.
 *   - Buoc 2: Tao socket TCP IPv4 va cho phep tai su dung dia chi.
 *   - Buoc 3: Bind socket vao cong, sau do listen() cho client.
 *   - Buoc 4: accept() ket noi va fork() mot tien trinh con xu ly.
 *   - Buoc 5: Tien trinh cha tiep tuc accept, tien trinh con dong socket
 *     server va xu ly frame bang handle_client().
 *
 * @return int:
 *   - 0 neu server ket thuc theo luong binh thuong.
 *   - EXIT_FAILURE neu tham so hoac system call khoi tao bi loi.
 */
int main(int argc, char **argv)
{
    struct sockaddr_in address; /* Dia chi IPv4 ma server se bind */
    struct sigaction action;    /* Cau hinh thu don tien trinh con */
    unsigned port;              /* Cong TCP server */
    unsigned buffer_count = DEFAULT_BUF; /* So o buffer vong moi client */
    unsigned drain_ms = 0;      /* Do tre ghi moi chunk, don vi mili-giay */
    unsigned ack_loss = 0;      /* Ty le bo ACK mo phong */
    int server_fd;              /* File descriptor socket lang nghe */

    /* =====================================================================
     * Step 1: Kiem tra tham so dong lenh
     * =====================================================================
     */
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

    /* =====================================================================
     * Step 2: Tao socket TCP IPv4
     * =====================================================================
     */
    /*
     * Hàm socket():
     *   - AF_INET: dùng địa chỉ IPv4.
     *   - SOCK_STREAM: dùng TCP, có kết nối và truyền byte theo thứ tự.
     *   - 0: chọn giao thức TCP mặc định.
     *   - Trả về descriptor >= 0 nếu thành công, -1 nếu lỗi.
     */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        /* perror() in nguyên nhân lỗi của system call socket(). */
        perror("socket");
        return EXIT_FAILURE;
    }
    {
        int reuse = 1; /* Cho phep bind lai cong sau khi server vua dung */
        /*
         * setsockopt() cấu hình thuộc tính socket.
         * SO_REUSEADDR giúp server bind lại địa chỉ/cổng đang ở trạng thái
         * chờ sau khi chương trình trước đó vừa đóng.
         */
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }

    /* =====================================================================
     * Step 3: Thiet lap dia chi va bind socket
     * =====================================================================
     */
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;             /* Họ địa chỉ IPv4 */
    address.sin_addr.s_addr = htonl(INADDR_ANY); /* Nhận kết nối trên mọi IP */
    address.sin_port = htons((uint16_t)port); /* Đổi port host sang network order */
    /*
     * Hàm bind():
     *   - server_fd: socket cần gắn địa chỉ.
     *   - &address: IP và port server muốn sử dụng.
     *   - sizeof(address): kích thước cấu trúc sockaddr_in.
     *   - Trả về 0 nếu bind thành công, -1 nếu port bận hoặc lỗi.
     */
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }
    /*
     * Hàm listen():
     *   - server_fd: socket đã bind.
     *   - 16: số kết nối tối đa chờ trong hàng đợi chưa accept.
     *   - Trả về 0 nếu bắt đầu lắng nghe thành công, -1 nếu lỗi.
     */
    if (listen(server_fd, 16) < 0) {
        perror("bind/listen");
        close(server_fd);
        return EXIT_FAILURE;
    }
    printf("TCP server started at port %u. Waiting for clients...\n", port);

    /* =====================================================================
     * Step 4: Accept va fork xu ly nhieu client
     * =====================================================================
     */
    for (;;) {
        struct sockaddr_in peer; /* Dia chi client vua ket noi */
        socklen_t peer_len = sizeof(peer); /* Kich thuoc cau truc dia chi */
        /*
         * Hàm accept():
         *   - server_fd: socket đang listen.
         *   - &peer: nơi nhận IP và port của client.
         *   - &peer_len: kích thước vào/ra của cấu trúc peer.
         *   - Trả về socket client riêng nếu thành công, -1 nếu lỗi.
         */
        int client_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        /*
         * Hàm fork():
         *   - Tạo tiến trình con sao chép trạng thái hiện tại.
         *   - Trả về 0 trong tiến trình con, PID con trong tiến trình cha,
         *     và -1 nếu không tạo được tiến trình.
         */
        if (fork() == 0) {
            /* Tien trinh con khong can socket lang nghe cua tien trinh cha. */
            close(server_fd);
            handle_client(client_fd, &peer, buffer_count, drain_ms, ack_loss);
            _exit(EXIT_SUCCESS);
        }
        /* Tien trinh cha chi giu server_fd va dong client_fd da giao con. */
        close(client_fd);
    }
    /* Dong socket lang nghe neu vong accept ket thuc do loi. */
    close(server_fd);
    return EXIT_FAILURE;
}
