/*
 * Concurrent TCP Server (HW6 - IT4062)
 *
 * Cách chạy:
 *     ./server PortNumber
 * Ví dụ:
 *     ./server 5500
 *
 * Chức năng:
 *   - Lắng nghe kết nối TCP IPv4 trên cổng được chỉ định.
 *   - Dùng fork() để phục vụ đồng thời nhiều client.
 *   - Nhận request STR hoặc FILE theo giao thức của bài tập.
 *   - Tính MD5, tách kết quả hoặc lưu file vào received_files/.
 */

#include "md5.h"                   /* Hàm gọi md5sum để tính MD5. */
#include "protocol.h"              /* Giao thức framing TCP dùng chung. */

#include <arpa/inet.h>             /* inet_ntop, htons và htonl. */
#include <ctype.h>                 /* isprint kiểm tra tệp văn bản. */
#include <errno.h>                 /* errno và EINTR. */
#include <limits.h>                /* PATH_MAX. */
#include <netinet/in.h>            /* struct sockaddr_in, AF_INET. */
#include <signal.h>                /* signal và SIGCHLD. */
#include <stdio.h>                 /* printf, fopen, fread, fwrite. */
#include <stdlib.h>                /* strtol, malloc, free, exit. */
#include <string.h>                /* strlen, strcmp, snprintf, strrchr. */
#include <sys/socket.h>            /* socket, bind, listen, accept, recv. */
#include <sys/stat.h>              /* mkdir, stat. */
#include <sys/types.h>             /* pid_t và socklen_t. */
#include <unistd.h>                /* close và fork. */

#define BACKLOG 10                 /* Số kết nối chờ tối đa trong listen queue. */

/**
 * @brief Tách MD5 hex thành hai chuỗi chữ và số.
 *
 * Các ký tự A-F được đưa vào @p letters, còn 0-9 được đưa vào @p digits.
 * Hai chuỗi kết quả được kết thúc bằng '\0' và sẽ được gửi thành hai dòng
 * phản hồi cho client.
 *
 * @param[in]  md5     Chuỗi MD5 gồm các ký tự hex in hoa.
 * @param[out] letters Bộ đệm nhận các chữ A-F.
 * @param[out] digits  Bộ đệm nhận các chữ số 0-9.
 */
static void split_md5(const char *md5, char *letters, char *digits)
{
    /*
     * Tách MD5 dạng hex in hoa thành phần chữ A-F và phần số 0-9.
     * Kết quả được gửi thành hai dòng riêng theo yêu cầu giao thức bài.
     */
    size_t letter_count = 0;       /* Số chữ cái đã tách. */
    size_t digit_count = 0;        /* Số chữ số đã tách. */
    size_t i;                      /* Chỉ số trong chuỗi MD5. */
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

/**
 * @brief Kiểm tra chuỗi có chỉ gồm chữ cái và chữ số ASCII hay không.
 *
 * Hàm duyệt đúng @p length byte, vì vậy không phụ thuộc vào việc chuỗi
 * có được kết thúc bằng '\0' hay không. Ký tự khoảng trắng, dấu câu và
 * byte điều khiển đều bị từ chối theo yêu cầu bài tập.
 *
 * @param[in] text   Vùng dữ liệu cần kiểm tra.
 * @param[in] length Số byte cần kiểm tra.
 *
 * @return `1` nếu mọi byte hợp lệ; `0` nếu có ít nhất một byte không hợp lệ.
 */
static int valid_string(const char *text, size_t length)
{
    /*
     * Chỉ chấp nhận chữ cái ASCII và chữ số ASCII.
     * Trả 1 nếu toàn bộ chuỗi hợp lệ, 0 nếu gặp ký tự khác.
     */
    size_t i;                      /* Chỉ số ký tự cần kiểm tra. */
    for (i = 0; i < length; ++i) {
        if (!((text[i] >= 'a' && text[i] <= 'z') ||
              (text[i] >= 'A' && text[i] <= 'Z') ||
              (text[i] >= '0' && text[i] <= '9'))) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Xử lý request STR và gửi kết quả MD5 cho client.
 *
 * Server ghi log chuỗi, kiểm tra dữ liệu, tính MD5 bằng md5_string(), rồi
 * tách MD5 thành hai dòng. Với chuỗi không hợp lệ, chỉ gửi một dòng lỗi;
 * quy ước này giúp client không đọc nhầm dòng lỗi thành phần MD5.
 *
 * @param[in] client_sock Socket phiên của client cần nhận phản hồi.
 * @param[in] text        Chuỗi đã được nhận đủ theo header STR.
 * @param[in] peer_name   Tên client dùng trong log, thường là IP:port.
 *
 * @return `0` khi gửi phản hồi thành công; `-1` khi send_all() thất bại.
 */
static int send_string_response(int client_sock, const char *text,
                                const char *peer_name)
{
    /*
     * Xử lý yêu cầu STR từ một client:
     * - kiểm tra chuỗi;
     * - nếu hợp lệ, tính MD5 và tách thành chữ/số;
     * - gửi hai dòng phản hồi về client.
     */
    char md5[MD5_DIGEST_LENGTH * 2 + 1]; /* MD5 xâu dạng hex in hoa. */
    char letters[MD5_DIGEST_LENGTH * 2 + 1]; /* Các chữ cái trong MD5. */
    char digits[MD5_DIGEST_LENGTH * 2 + 1]; /* Các chữ số trong MD5. */
    char response[MD5_DIGEST_LENGTH * 2 + 2]; /* Header + newline tối đa. */

    printf("\n[%s] Received string:\n\"%s\"\n", peer_name, text);
    if (!valid_string(text, strlen(text))) {
        const char *error = "Error: String contains non-alphanumeric characters\n";
        printf("-> Error: String contains non-alphanumeric characters. "
               "Error message sent.\n");
        return send_all(client_sock, error, strlen(error));
    }
    md5_string(text, md5);
    split_md5(md5, letters, digits);
    printf("MD5          : %s\nLetters      : %s\nDigits       : %s\n",
           md5, letters, digits);

    /*
     * Hai send riêng biệt đúng yêu cầu đề bài. Header newline giúp client
     * biết điểm kết thúc từng xâu trong TCP stream.
     */
    snprintf(response, sizeof(response), "%s\n", letters);
    if (send_all(client_sock, response, strlen(response)) < 0) {
        return -1;
    }
    snprintf(response, sizeof(response), "%s\n", digits);
    return send_all(client_sock, response, strlen(response));
}

/**
 * @brief Kiểm tra file có an toàn để in nội dung lên terminal không.
 *
 * File được đọc ở chế độ nhị phân để giữ nguyên byte. Byte NUL và ký tự
 * điều khiển ngoài newline, carriage return, tab làm file được xem là
 * nhị phân; các file như vậy vẫn được lưu và tính MD5 nhưng không in ra.
 *
 * @param[in] path Đường dẫn file cần kiểm tra.
 *
 * @return `1` nếu file mở được và chỉ chứa nội dung text cho phép;
 *         `0` nếu không mở được hoặc phát hiện byte không phù hợp.
 */
static int is_text_file(const char *path)
{
    /*
     * Kiểm tra nhanh xem file có thể in an toàn lên terminal hay không.
     * File chứa byte NUL hoặc ký tự điều khiển ngoài \n, \r, \t được
     * xem là file nhị phân và không in nội dung.
     */
    FILE *input = fopen(path, "rb"); /* Mở nhị phân để không đổi byte. */
    int character;                  /* Byte hiện tại. */
    int text = 1;                   /* Cờ kết luận tệp văn bản. */
    if (input == NULL) {
        return 0;
    }
    while ((character = fgetc(input)) != EOF) {
        if (character == '\0' ||
            (!isprint((unsigned char)character) &&
             character != '\n' && character != '\r' && character != '\t')) {
            text = 0;
            break;
        }
    }
    fclose(input);
    return text;
}

/**
 * @brief Nhận, lưu và xác thực một file từ client.
 *
 * Header có dạng "FILE <size> <name>\n". Hàm kiểm tra header và giới hạn
 * kích thước, tạo thư mục received_files/, nhận đúng số byte đã khai báo,
 * ghi file ở chế độ nhị phân và cập nhật MD5 theo từng chunk. Sau khi nhận
 * xong, server gửi MD5 dạng 32 ký tự hex kèm newline về client.
 *
 * @param[in] client_sock Socket phiên dùng để nhận file.
 * @param[in] header      Header FILE đã đọc bởi recv_line().
 * @param[in] peer_name   Định danh client dạng IP:port để đặt tên file/log.
 *
 * @return `0` khi nhận và gửi MD5 thành công; `-1` khi lỗi socket, file
 *         hoặc tạo đường dẫn; với header sai, gửi lỗi rồi trả kết quả send.
 */
static int send_file_response(int client_sock, const char *header,
                              const char *peer_name)
{
    /*
     * Xử lý yêu cầu FILE từ client:
     * - đọc kích thước và tên file trong header;
     * - tạo thư mục đích và lưu dữ liệu theo từng chunk;
     * - tính MD5 ngay trong lúc nhận;
     * - in thông tin file và gửi MD5 về client.
     */
    unsigned long long file_size;    /* Kích thước lấy từ header. */
    char filename[MAX_FILENAME_SIZE + 1]; /* Tên tệp trong header. */
    char saved_path[PATH_MAX];       /* Tên tệp đích. */
    unsigned char buffer[CHUNK_SIZE]; /* Bộ đệm nhận từng chunk 8 KB. */
    FILE *output;                    /* Tệp đích mở ghi nhị phân. */
    MD5_CTX context;                 /* Trạng thái MD5 streaming. */
    unsigned char digest[MD5_DIGEST_LENGTH]; /* MD5 nhị phân cuối cùng. */
    char md5[MD5_DIGEST_LENGTH * 2 + 1]; /* MD5 dạng hex in hoa. */
    unsigned long long remaining;    /* Số byte còn nhận. */
    const char *separator;           /* Dấu cách giữa size và name. */

    /* Header phải có đúng kích thước hợp lệ và tên file không vượt giới hạn. */
    if (sscanf(header, "FILE %llu %255s", &file_size, filename) != 2 ||
        file_size >= MAX_FILE_SIZE) {
        return send_all(client_sock, "Error: invalid file header\n", 26);
    }
    /* Tách IP và port để tạo tên file không bị trùng giữa các client. */
    separator = strrchr(peer_name, ':');
    if (mkdir("received_files", 0755) < 0 && errno != EEXIST) {
        perror("mkdir received_files");
        return -1;
    }
    if (snprintf(saved_path, sizeof(saved_path), "received_files/%.*s_%s_%s",
                 separator == NULL ? (int)strlen(peer_name) :
                 (int)(separator - peer_name), peer_name,
                 separator == NULL ? "0" : separator + 1, filename) >=
        (int)sizeof(saved_path)) {
        return -1;
    }
    output = fopen(saved_path, "wb");
    if (output == NULL) {
        perror("fopen");
        return -1;
    }

    /* Nhận đúng file_size byte; không đọc thừa sang request kế tiếp. */
    md5_init(&context);
    remaining = file_size;
    while (remaining > 0) {
        size_t wanted = remaining > CHUNK_SIZE ? CHUNK_SIZE : (size_t)remaining;
        if (recv_all(client_sock, buffer, wanted) < 0 ||
            fwrite(buffer, 1, wanted, output) != wanted) {
            fclose(output);
            return -1;
        }
        md5_update(&context, buffer, wanted);
        remaining -= wanted;
    }
    fclose(output);
    md5_final(digest, &context);
    md5_hex(digest, md5);

    printf("\n[%s] Received file:\n\"%s\"\nSaved at    :\n%s\n"
           "File size   : %llu bytes\n", peer_name, filename, saved_path,
           file_size);
    /* Chỉ in nội dung nếu file là text để không làm hỏng terminal. */
    if (is_text_file(saved_path)) {
        FILE *input = fopen(saved_path, "r"); /* Mở text để hiển thị nội dung. */
        int character;                       /* Ký tự in ra terminal. */
        printf("----------------------- FILE CONTENT -----------------------\n");
        if (input != NULL) {
            while ((character = fgetc(input)) != EOF) {
                putchar(character);
            }
            fclose(input);
        }
        printf("\n----------------------- END CONTENT ------------------------\n");
    }
    printf("MD5         : %s\n", md5);
    {
        char response[MD5_DIGEST_LENGTH * 2 + 2]; /* MD5 + newline. */
        snprintf(response, sizeof(response), "%s\n", md5);
        return send_all(client_sock, response, strlen(response));
    }
}

/**
 * @brief Phục vụ toàn bộ request của một client trong tiến trình con.
 *
 * Hàm đọc từng header đến khi client đóng kết nối hoặc gửi request sai.
 * Header STR làm hàm nhận chuỗi rồi xử lý MD5; header FILE ủy quyền cho
 * send_file_response(). Khi vòng lặp kết thúc, socket phiên được đóng.
 *
 * @param[in] client_sock Socket đã được accept dành riêng cho client.
 * @param[in] peer_name   Định danh IP:port dùng trong thông báo log.
 */
static void serve_client(int client_sock, const char *peer_name)
{
    /*
     * Vòng lặp phục vụ một client trong tiến trình con.
     * Mỗi request bắt đầu bằng header STR hoặc FILE; header quyết định
     * số byte tiếp theo cần nhận và hàm xử lý tương ứng.
     */
    char header[MAX_LINE_SIZE];       /* Header STR hoặc FILE. */
    for (;;) {
        int length;
        /*
         * recv_line() dùng recv() đọc tới '\n'. Trả -1 khi client close,
         * đây là cách phát hiện kết thúc phiên TCP.
         */
        length = recv_line(client_sock, header, sizeof(header));
        if (length < 0) {
            printf("[%s] Client disconnected.\n", peer_name);
            break;
        }
        if (strncmp(header, "STR ", 4) == 0) {
            unsigned long string_length; /* Độ dài xâu trong header. */
            char *text;                  /* Bộ nhớ nhận xâu. */
            /* Kiểm tra độ dài trước khi cấp phát để tránh request bất hợp lệ. */
            if (sscanf(header, "STR %lu", &string_length) != 1 ||
                string_length == 0 || string_length > MAX_STRING_SIZE) {
                break;
            }
            text = malloc(string_length + 1);
            if (text == NULL || recv_all(client_sock, text, string_length) < 0) {
                free(text);
                break;
            }
            text[string_length] = '\0';
            if (send_string_response(client_sock, text, peer_name) < 0) {
                free(text);
                break;
            }
            free(text);
        } else if (strncmp(header, "FILE ", 5) == 0) {
            /* Hàm con tự đọc chính xác số byte file từ header. */
            if (send_file_response(client_sock, header, peer_name) < 0) {
                break;
            }
        } else {
            break;
        }
    }
    close(client_sock);                /* Socket phiên của tiến trình con. */
}

/**
 * @brief TCP server xử lý đồng thời nhiều client bằng fork().
 *
 * Chi tiết luồng xử lý:
 *   - Bước 1: Kiểm tra PortNumber, tạo socket AF_INET/SOCK_STREAM.
 *   - Bước 2: Gán địa chỉ bằng bind(), chuyển sang listen().
 *   - Bước 3: accept() một client và fork() tiến trình con.
 *   - Bước 4: Con nhận header STR/FILE, xử lý dữ liệu, send phản hồi.
 *   - Bước 5: Con close socket phiên; cha tiếp tục accept client khác.
 *
 * @param[in] argc Số lượng đối số dòng lệnh; phải bằng 2.
 * @param[in] argv argv[1] là số hiệu cổng từ 1 đến 65535.
 * @return 0 khi kết thúc bình thường; khác 0 nếu tham số hoặc system call lỗi.
 */
int main(int argc, char *argv[])
{
    long port;                         /* Cổng host order sau khi kiểm tra. */
    char *end;                         /* Vị trí kết thúc chuỗi số. */
    int server_sock;                   /* Socket lắng nghe. */
    int option = 1;                    /* Bật SO_REUSEADDR. */
    struct sockaddr_in server_addr;    /* Địa chỉ IPv4 server. */

    /* =========================================================================
     * Step 1: Kiểm tra cổng và tạo socket TCP IPv4.
     * =========================================================================
     */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s PortNumber\n", argv[0]);
        return 1;
    }
    errno = 0;
    port = strtol(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Usage: %s PortNumber\n", argv[0]);
        return 1;
    }
    /*
     * socket(AF_INET, SOCK_STREAM, 0):
     * AF_INET là IPv4; SOCK_STREAM là TCP tin cậy; 0 chọn giao thức mặc định.
     * Kết quả là file descriptor >= 0, hoặc -1 nếu system call lỗi.
     */
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,
                   &option, sizeof(option)) < 0) {
        perror("setsockopt");
        close(server_sock);
        return 1;
    }

    /* =========================================================================
     * Step 2: Thiết lập địa chỉ, bind và listen.
     * =========================================================================
     */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short)port);
    /*
     * bind(): gắn socket với IP/cổng; htons chuyển cổng từ host order
     * sang network byte order. Trả 0 thành công, -1 nếu lỗi.
     */
    if (bind(server_sock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return 1;
    }
    /*
     * listen(): đưa socket vào trạng thái chờ kết nối TCP. BACKLOG là
     * số kết nối chưa được accept có thể chờ trong hàng đợi.
     */
    if (listen(server_sock, BACKLOG) < 0) {
        perror("listen");
        close(server_sock);
        return 1;
    }
    signal(SIGCHLD, SIG_IGN);
    printf("TCP server started at port %ld. Waiting for clients...\n", port);

    /* =========================================================================
     * Step 3: accept và fork một tiến trình cho từng client.
     * =========================================================================
     */
    for (;;) {
        struct sockaddr_in client_addr; /* Địa chỉ client. */
        socklen_t client_length = sizeof(client_addr); /* Kích thước địa chỉ. */
        int client_sock;                /* Socket riêng sau accept. */
        char client_ip[INET_ADDRSTRLEN]; /* IP dạng văn bản. */
        char peer_name[INET_ADDRSTRLEN + 16]; /* IP:port trong log. */
        /*
         * accept(): lấy kết nối khỏi hàng đợi và tạo socket mới cho client.
         * Socket server_sock vẫn tiếp tục listen. Trả -1 khi lỗi.
         */
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr,
                             &client_length);
        if (client_sock < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        snprintf(peer_name, sizeof(peer_name), "%s:%u", client_ip,
                 (unsigned)ntohs(client_addr.sin_port));
        printf("[%s] Client connected.\n", peer_name);
        /*
         * fork(): 0 ở tiến trình con, PID con ở cha, -1 nếu lỗi.
         * Con đóng socket listen và phục vụ client; cha đóng client socket.
         */
        {
            pid_t child = fork();
            if (child < 0) {
                perror("fork");
                close(client_sock);
                continue;
            }
            if (child == 0) {
                close(server_sock);
                serve_client(client_sock, peer_name);
                exit(0);
            }
        }
        close(client_sock);
    }
    close(server_sock);
    return 0;
}
