#include "md5.h"                   /* Hàm gọi md5sum để tính MD5. */
#include "protocol.h"              /* Hàm send_all, recv_line và hằng số. */

#include <arpa/inet.h>             /* inet_pton và htons. */
#include <errno.h>                 /* errno khi strtol/stat lỗi. */
#include <netinet/in.h>            /* struct sockaddr_in và AF_INET. */
#include <stdio.h>                 /* printf, fgets, fread, fopen. */
#include <stdlib.h>                /* strtol, malloc, free. */
#include <string.h>                /* strlen, strcmp, strcspn, strrchr. */
#include <sys/socket.h>            /* socket, connect. */
#include <sys/stat.h>              /* stat và S_ISREG. */
#include <unistd.h>                /* close. */

static void remove_newline(char *text)
{
    text[strcspn(text, "\n")] = '\0'; /* Xóa newline do fgets giữ lại. */
}

static void split_md5(const char *md5, char *letters, char *digits)
{
    size_t letter_count = 0;       /* Số chữ cái đã tách. */
    size_t digit_count = 0;        /* Số chữ số đã tách. */
    size_t i;                      /* Chỉ số chuỗi MD5. */
    for (i = 0; md5[i] != '\0'; ++i) {
        if (md5[i] >= 'A' && md5[i] <= 'F') {
            letters[letter_count++] = md5[i];
        } else {
            digits[digit_count++] = md5[i];
        }
    }
    letters[letter_count] = '\0';
    digits[digit_count] = '\0';
}

static int send_string(int sockfd, const char *text)
{
    char header[64];                /* Header STR <len>\n. */
    char letters[MD5_DIGEST_LENGTH * 2 + 1]; /* Chữ client tự tách. */
    char digits[MD5_DIGEST_LENGTH * 2 + 1];  /* Số client tự tách. */
    char server_letters[128];       /* Chữ hoặc thông báo lỗi server trả. */
    char server_digits[MD5_DIGEST_LENGTH * 2 + 2];  /* Số + newline + '\0'. */
    char local_md5[MD5_DIGEST_LENGTH * 2 + 1]; /* MD5 cục bộ. */

    snprintf(header, sizeof(header), "STR %zu\n", strlen(text));
    /*
     * send_all() gửi header và nội dung. TCP là byte stream nên hàm này
     * lặp send() đến khi đủ số byte, tránh gửi thiếu khi buffer lớn.
     */
    if (send_all(sockfd, header, strlen(header)) < 0 ||
        send_all(sockfd, text, strlen(text)) < 0) {
        return -1;
    }
    /*
     * recv_line() nhận lần lượt hai dòng letters và digits do server gửi
     * bằng hai lần send riêng biệt. Dấu newline là delimiter ứng dụng.
     */
    if (recv_line(sockfd, server_letters, sizeof(server_letters)) < 0) {
        return -1;
    }
    /*
     * Với xâu không hợp lệ, server chỉ gửi một dòng Error. Không được
     * cố đọc thêm dòng thứ hai vì như vậy sẽ làm lệch giao thức TCP.
     */
    if (strncmp(server_letters, "Error:", 6) == 0) {
        printf("%s\n", server_letters);
        return 0;
    }
    if (recv_line(sockfd, server_digits, sizeof(server_digits)) < 0) {
        return -1;
    }
    md5_string(text, local_md5);
    split_md5(local_md5, letters, digits);
    printf("Letters: %s\nDigits : %s\nCheck  : %s\n",
           server_letters, server_digits,
           strcmp(letters, server_letters) == 0 &&
           strcmp(digits, server_digits) == 0 ? "Match" : "Mismatch");
    return 0;
}

static int send_file(int sockfd, const char *path)
{
    struct stat info;               /* Metadata tệp từ stat. */
    FILE *input;                    /* Tệp mở nhị phân để fread. */
    const char *filename;           /* Basename gửi trong header. */
    unsigned char buffer[CHUNK_SIZE]; /* Chunk đọc và gửi. */
    size_t bytes_read;              /* Số byte fread được. */
    MD5_CTX context;                /* Trạng thái MD5 tệp. */
    unsigned char digest[MD5_DIGEST_LENGTH]; /* Digest nhị phân. */
    char local_md5[MD5_DIGEST_LENGTH * 2 + 1]; /* MD5 nguồn. */
    char server_md5[MD5_DIGEST_LENGTH * 2 + 2]; /* MD5 + newline + '\0'. */
    char header[MAX_LINE_SIZE];     /* Header FILE <size> <name>\n. */

    if (stat(path, &info) < 0) {
        fprintf(stderr, "Error: File '%s' not found.\n", path);
        return 0;
    }
    if (!S_ISREG(info.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a regular file.\n", path);
        return 0;
    }
    if (info.st_size < 0 || (uint64_t)info.st_size >= MAX_FILE_SIZE) {
        fprintf(stderr, "Error: File size must be less than 10MB.\n");
        return 0;
    }
    filename = strrchr(path, '/');
    filename = filename == NULL ? path : filename + 1;
    if (*filename == '\0' || strlen(filename) > MAX_FILENAME_SIZE) {
        fprintf(stderr, "Error: invalid filename.\n");
        return 0;
    }
    input = fopen(path, "rb");
    if (input == NULL) {
        perror("fopen");
        return -1;
    }
    snprintf(header, sizeof(header), "FILE %llu %s\n",
             (unsigned long long)info.st_size, filename);
    if (send_all(sockfd, header, strlen(header)) < 0) {
        fclose(input);
        return -1;
    }
    md5_init(&context);
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        /*
         * fread() đọc từng chunk 8 KB; send_all() truyền đủ chunk.
         * MD5 được cập nhật cùng dữ liệu để không cần đọc tệp lần hai.
         */
        if (send_all(sockfd, buffer, bytes_read) < 0) {
            fclose(input);
            return -1;
        }
        md5_update(&context, buffer, bytes_read);
    }
    if (ferror(input)) {
        fclose(input);
        return -1;
    }
    fclose(input);
    md5_final(digest, &context);
    md5_hex(digest, local_md5);
    printf("Sent %lld bytes of file '%s' to server.\nMD5: %s\n",
           (long long)info.st_size, filename, local_md5);
    if (recv_line(sockfd, server_md5, sizeof(server_md5)) < 0) {
        return -1;
    }
    printf("Check: %s\n",
           strcmp(local_md5, server_md5) == 0 ? "Match" : "Mismatch");
    return 0;
}

static void print_menu(void)
{
    printf("\nMENU\n-----------------------------------\n"
           "1. Send any string\n"
           "2. Send file content\n"
           "3. Exit\n\nYour choice: ");
}

/**
 * @brief TCP client kết nối server, gửi xâu/tệp và kiểm tra MD5.
 *
 * Chi tiết luồng xử lý:
 *   - Bước 1: Kiểm tra IPv4/cổng và tạo socket AF_INET/SOCK_STREAM.
 *   - Bước 2: connect() tới địa chỉ server.
 *   - Bước 3: Hiển thị menu; gửi STR hoặc FILE theo lựa chọn.
 *   - Bước 4: recv() phản hồi, tự tính MD5 bằng md5sum để đối chiếu.
 *   - Bước 5: Lựa chọn 3 hoặc EOF thì close socket và thoát mã 0.
 *
 * @param[in] argc Số đối số dòng lệnh; phải bằng 3.
 * @param[in] argv argv[1] là IPv4, argv[2] là cổng server.
 * @return 0 khi thoát bình thường; khác 0 nếu tham số/kết nối lỗi.
 */
int main(int argc, char *argv[])
{
    long port;                     /* Cổng host order. */
    char *end;                     /* Vị trí kết thúc số strtol. */
    int client_sock;               /* Descriptor socket TCP client. */
    struct sockaddr_in server_addr; /* Địa chỉ IPv4 server. */
    char input[4096];              /* Bộ đệm nhập menu, xâu, đường dẫn. */

    /* =========================================================================
     * Step 1: Kiểm tra IP/cổng và tạo socket.
     * =========================================================================
     */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s IPAddress PortNumber\n", argv[0]);
        return 1;
    }
    errno = 0;
    port = strtol(argv[2], &end, 10);
    if (errno != 0 || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Usage: %s IPAddress PortNumber\n", argv[0]);
        return 1;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    /*
     * inet_pton(): đổi IPv4 dạng text sang binary network order.
     * Trả 1 nếu hợp lệ, 0 nếu sai định dạng, -1 nếu họ địa chỉ lỗi.
     */
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        return 1;
    }
    server_addr.sin_port = htons((unsigned short)port);
    /*
     * socket(): AF_INET = IPv4, SOCK_STREAM = TCP, 0 = protocol mặc định.
     * Trả file descriptor hoặc -1 khi lỗi.
     */
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock < 0) {
        perror("socket");
        return 1;
    }

    /* =========================================================================
     * Step 2: connect tới server.
     * =========================================================================
     */
    /*
     * connect(): bắt tay TCP tới IP/cổng trong server_addr.
     * Trả 0 khi thành công, -1 khi server không tồn tại/từ chối kết nối.
     */
    if (connect(client_sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(client_sock);
        return 1;
    }

    /* =========================================================================
     * Step 3: Menu và xử lý từng lựa chọn.
     * =========================================================================
     */
    for (;;) {
        print_menu();
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("Connection closed. Goodbye!\n");
            break;
        }
        if (input[0] == '1') {
            for (;;) {
                printf("Enter string: ");
                if (fgets(input, sizeof(input), stdin) == NULL) {
                    goto done;
                }
                remove_newline(input);
                if (input[0] == '\0') {
                    break;
                }
                if (send_string(client_sock, input) < 0) {
                    perror("send/recv string");
                    goto error;
                }
            }
        } else if (input[0] == '2') {
            printf("Enter file path: ");
            if (fgets(input, sizeof(input), stdin) == NULL) {
                goto done;
            }
            remove_newline(input);
            if (input[0] != '\0' && send_file(client_sock, input) < 0) {
                perror("send/recv file");
                goto error;
            }
        } else if (input[0] == '3') {
            printf("Connection closed. Goodbye!\n");
            break;
        } else {
            fprintf(stderr, "Invalid choice. Please choose 1, 2 or 3.\n");
        }
    }
done:
    close(client_sock);             /* Đóng kết nối TCP trước khi thoát. */
    return 0;
error:
    close(client_sock);
    return 1;
}
