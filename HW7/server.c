#define _GNU_SOURCE
#include "proto.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    uint8_t *data;
    size_t len;
    int present;
} slot_t;

typedef struct {
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

static void reap_children(int signal_number)
{
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

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

static void handle_client(int fd, const struct sockaddr_in *peer,
                          unsigned buffer_count, unsigned drain_ms,
                          unsigned ack_loss)
{
    uint8_t payload[MAX_PAYLOAD];
    frame_hdr_t header;
    transfer_t transfer;
    int active = 0;

    memset(&transfer, 0, sizeof(transfer));
    transfer.fd = fd;
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
        result = recv_frame(fd, &header, payload);
        if (result == RF_EOF || result == RF_ERR) {
            break;
        }
        if (result == RF_CRC || result == RF_PROTO) {
            continue;
        }
        if (!active && header.type == T_STR) {
            if (header.len <= MAX_STR &&
                send_string_reply(fd, payload, header.len) < 0) {
                break;
            }
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
        } else if (active && header.type == T_DATA) {
            if (receive_file(&transfer, &header, payload) < 0) {
                break;
            }
        } else if (active && header.type == T_PROBE) {
            if (send_ack(&transfer) < 0) {
                break;
            }
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
    if (active) {
        free_transfer(&transfer, 1);
    } else {
        free_transfer(&transfer, 0);
    }
    close(fd);
}

int main(int argc, char **argv)
{
    struct sockaddr_in address;
    struct sigaction action;
    unsigned port;
    unsigned buffer_count = DEFAULT_BUF;
    unsigned drain_ms = 0;
    unsigned ack_loss = 0;
    int server_fd;

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
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    {
        int reuse = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(server_fd, 16) < 0) {
        perror("bind/listen");
        close(server_fd);
        return EXIT_FAILURE;
    }
    printf("TCP server started at port %u. Waiting for clients...\n", port);
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int client_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        if (fork() == 0) {
            close(server_fd);
            handle_client(client_fd, &peer, buffer_count, drain_ms, ack_loss);
            _exit(EXIT_SUCCESS);
        }
        close(client_fd);
    }
    close(server_fd);
    return EXIT_FAILURE;
}
