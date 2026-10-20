#define _GNU_SOURCE
#include "proto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    long long sent_at;
    int outstanding;
} sent_t;

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

static int send_data(int fd, int file_fd, uint32_t seq, uint32_t len,
                     unsigned loss, unsigned corrupt)
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
    if (chance((double)loss)) {
        return 0;
    }
    frame_len = build_frame(frame, T_DATA, 0, seq, payload,
                            sizeof(uint32_t) + len);
    if (frame_len == 0) {
        return -1;
    }
    if (chance((double)corrupt)) {
        frame[HDR_LEN + sizeof(uint32_t)] ^= 0x01;
    }
    return send_all(fd, frame, frame_len) < 0 ? -1 : 0;
}

static uint32_t chunk_length(uint64_t file_size, uint32_t seq)
{
    uint64_t offset = (uint64_t)seq * CHUNK_SIZE;
    uint64_t remaining = file_size - offset;
    return remaining > CHUNK_SIZE ? CHUNK_SIZE : (uint32_t)remaining;
}

static int send_file(int fd, const char *path, unsigned loss, unsigned corrupt)
{
    struct stat info;
    frame_hdr_t header;
    uint8_t payload[MAX_PAYLOAD];
    const char *name;
    uint32_t total;
    uint32_t base = 0;
    uint32_t next = 0;
    uint32_t edge;
    int file_fd;
    sent_t sent[MAX_INFLIGHT];
    md5_t *md5;
    char local_md5[33];
    unsigned char buffer[CHUNK_SIZE];
    ssize_t read_count;

    if (stat(path, &info) < 0) {
        perror("stat");
        return 0;
    }
    if (!S_ISREG(info.st_mode)) {
        fprintf(stderr, "Error: not a regular file\n");
        return 0;
    }
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
        int zero_window = header.wnd == 0;
        long long persist_at = now_ms() + PERSIST_MS;
    while (base < total) {
        while (next < total && next < edge && next < base + MAX_INFLIGHT) {
            uint32_t len = chunk_length((uint64_t)info.st_size, next);
            if (send_data(fd, file_fd, next, len, loss, corrupt) < 0) {
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
            int poll_result = poll(&pfd, 1, timeout);
            if (poll_result < 0 && errno == EINTR) {
                continue;
            }
            if (poll_result < 0) {
                close(file_fd);
                return -1;
            }
            if (poll_result == 0) {
                if (zero_window && next == edge) {
                    if (send_frame(fd, T_PROBE, 0, base, NULL, 0) < 0) {
                        close(file_fd);
                        return -1;
                    }
                    persist_at = now_ms() + PERSIST_MS;
                } else if (base < next) {
                    uint32_t retry = base;
                    if (send_data(fd, file_fd, retry,
                                  chunk_length((uint64_t)info.st_size, retry),
                                  loss, corrupt) < 0) {
                        close(file_fd);
                        return -1;
                    }
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
        if (header.seq == last_ack) {
            duplicate_acks++;
            if (duplicate_acks >= 3 && base < next) {
                uint32_t retry = base;
                if (send_data(fd, file_fd, retry,
                              chunk_length((uint64_t)info.st_size, retry),
                              loss, corrupt) < 0) {
                    close(file_fd);
                    return -1;
                }
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
            duplicate_acks = 0;
            rto = RTO_INIT_MS;
            edge = header.seq + header.wnd;
            zero_window = header.wnd == 0;
            persist_at = now_ms() + PERSIST_MS;
        }
    }
    }
    close(file_fd);
    if (send_frame(fd, T_FEND, 0, total, NULL, 0) < 0 ||
        recv_frame(fd, &header, payload) != RF_OK ||
        header.type != T_FMD5 || header.len != 32) {
        return -1;
    }
    printf("MD5: %s\nCheck: %s\n", local_md5,
           memcmp(local_md5, payload, 32) == 0 ? "Match" : "Mismatch");
    return 0;
}

int main(int argc, char **argv)
{
    struct sockaddr_in address;
    unsigned port;
    unsigned loss = 0;
    unsigned corrupt = 0;
    int fd;

    if (argc < 3 || argc > 5 ||
        parse_port(argv[2], &port) < 0 ||
        (argc > 3 && parse_percent(argv[3], &loss) < 0) ||
        (argc > 4 && parse_percent(argv[4], &corrupt) < 0) ||
        inet_pton(AF_INET, argv[1], &address.sin_addr) != 1) {
        fprintf(stderr, "Usage: %s <IPAddress> <Port> [Loss%%] [Corrupt%%]\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    srand((unsigned)(getpid() ^ (unsigned)now_ms()));
    signal(SIGPIPE, SIG_IGN);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1 ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connect");
        close(fd);
        return EXIT_FAILURE;
    }
    for (;;) {
        char line[2048];
        printf("MENU\n-----------------------------------\n"
               "1. Send any string\n2. Send file content\n3. Exit\n"
               "Your choice: ");
        fflush(stdout);
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
            if (line[0] != '\0' && send_string(fd, line) < 0) {
                break;
            }
        } else if (line[0] == '2') {
            printf("Enter file path: ");
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
    printf("Connection closed. Goodbye!\n");
    close(fd);
    return EXIT_SUCCESS;
}
