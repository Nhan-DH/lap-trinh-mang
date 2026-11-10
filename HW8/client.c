/*
 * client.c - Client chat TCP đa luồng (BÀI LÀM CỦA SINH VIÊN)
 *
 * Cú pháp: ./client IPAddress PortNumber
 *
 * Đã cung cấp sẵn: biến toàn cục, hàm say() (in + flush), tách token,
 * main() (kết nối, tạo luồng nhận, vòng lặp đọc stdin).
 *
 * SINH VIÊN PHẢI HOÀN THIỆN các hàm đánh dấu TODO. Xem đề bài, mục 3 và 5.
 * Ghi lên socket và in ra màn hình đều phải được khóa mutex.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "common.h"

static int g_fd = -1;
pthread_mutex_t g_wlock = PTHREAD_MUTEX_INITIALIZER;  /* khóa ghi socket */
static pthread_mutex_t g_plock = PTHREAD_MUTEX_INITIALIZER;  /* khóa in ra màn hình */

/* In một dòng và flush ngay (ĐÃ CUNG CẤP).
 * Lưu ý: khi bị chấm tự động, stdout là pipe nên phải flush sau mỗi dòng. */
/*
 * @brief In một dòng message ra stdout an toàn đa luồng.
 * @input fmt: định dạng kiểu printf; ...: các đối số tương ứng.
 * @output Không trả giá trị; luôn thêm newline và fflush stdout.
 * @detail g_plock giúp hai luồng không in xen kẽ; flush để bộ chấm đọc pipe.
 */
static void say(const char *fmt, ...)
{
    va_list ap;

    pthread_mutex_lock(&g_plock);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
    pthread_mutex_unlock(&g_plock);
}

/* Tách token (ĐÃ CUNG CẤP). Trả 1 nếu có, 0 nếu hết, -1 nếu quá dài. */
/*
 * @brief Tách tham số khỏi chuỗi lệnh người dùng.
 * @input s: vị trí đọc hiện tại; out: chuỗi đích; cap: dung lượng.
 * @output 1: token; 0: hết; -1: vượt giới hạn.
 * @detail Dùng cho /register, /login, /msg, /file.
 */
int take_token(char **s, char *out, size_t cap)
{
    size_t n;

    while (**s == ' ')
        (*s)++;
    if (**s == '\0')
        return 0;
    n = strcspn(*s, " ");
    if (n >= cap)
        return -1;
    memcpy(out, *s, n);
    out[n] = '\0';
    *s += n;
    return 1;
}

/* Trả 1 nếu chỉ còn khoảng trắng (ĐÃ CUNG CẤP). */
/*
 * @brief Kiểm tra phần lệnh sau tham số chỉ có khoảng trắng.
 * @input s: chuỗi phần còn lại.
 * @output 1: không có tham số thừa; 0: có dư.
 * @detail Ngăn lệnh có tham số ngoài cú pháp.
 */
int at_end(char *s)
{
    while (*s == ' ')
        s++;
    return *s == '\0';
}

/* ------------------------------------------------------------------ */
/* Gửi lệnh lên server                                                  */
/* ------------------------------------------------------------------ */

/* TODO 1: Gửi một frame lên server qua g_fd. Phải khóa g_wlock quanh send_msg().
 *   Trả 0 nếu thành công, -1 nếu lỗi. */
/*
 * @brief Đóng gói, mã hóa AES-256-CBC và gửi đúng một frame qua TCP.
 * @input hdr: lệnh và tham số (UTF-8); body: dữ liệu file hoặc NULL;
 *        blen: số byte của body (không dùng strlen với file nhị phân).
 * @output 0 nếu thành công; -1 nếu gửi lỗi; không sở hữu/free body.
 * @mutex g_wlock chống ghi xen kẽ khi nhiều luồng gửi cùng socket.
 */
int send_cmd(const char *hdr, const void *body, size_t blen)
{
    int rc;

    /* =========================================================================
     * Step 1: Khóa quyền ghi socket client
     * Đảm bảo một frame AES-256-CBC không bị xen kẽ bởi frame khác.
     * ========================================================================= */
    pthread_mutex_lock(&g_wlock);

    /* =========================================================================
     * Step 2: Đóng gói frame và gửi qua TCP
     * send_msg từ net.c ghép HEADER+newline+BODY, mã hóa và gọi send_all/send().
     * ========================================================================= */
    rc = send_msg(g_fd, hdr, body, blen);

    /* =========================================================================
     * Step 3: Mở khóa và trả kết quả
     * 0 = thành công; -1 = lỗi mã hóa/TCP/cấp phát.
     * ========================================================================= */
    pthread_mutex_unlock(&g_wlock);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Nhận và hiển thị frame từ server                                     */
/* ------------------------------------------------------------------ */

/* TODO 2: Xử lý một frame nhận được từ server (plain đã giải mã, hủy bằng caller).
 *   Dùng split_msg() để tách header và body, rồi nhận dạng header:
 *
 *   "OK <msg>"                       -> say("[OK] %s", msg)
 *   "ERR <code> <msg>"               -> say("[ERR %d] %s", code, msg)
 *   "MSG <from> <nội dung>"          -> say("[MSG] %s: %s", from, nội_dung)
 *   "FILE <from> <tên> <size>" + body-> ghi body vào downloads/<from>_<tên>
 *                                       (tạo thư mục downloads nếu chưa có, dùng
 *                                       mkdir(DOWNLOAD_DIR, 0755)), rồi
 *                                       say("[FILE] %s: %s (%zu bytes) -> %s",
 *                                           from, tên, size, đường_dẫn)
 *   Lỗi khác (không khớp, size không bằng độ dài body, ...): say("[ERR] ...").
 *
 *   Gợi ý: sscanf(header, "MSG %63s %n", from, &n) — %n cho biết vị trí bắt đầu
 *   nội dung tin nhắn. Đừng quên kiểm tra tên từ server không chứa '/' hoặc '\\'. */
/*
 * @brief Phân tích một frame phản hồi/forward đã được giải mã.
 * @input plain: bytes plaintext HEADER\nBODY; plen: độ dài bytes.
 * @output In đúng định dạng [OK], [ERR <code>], [MSG], [FILE]; FILE được
 *         lưu ở downloads/<sender>_<filename>. Không giải phóng plain.
 */
void handle_incoming(unsigned char *plain, size_t plen)
{
    char header[MAX_HEADER_LEN + 1];
    const unsigned char *body = NULL;
    size_t blen = 0;
    char *p;
    char action[16];


    /* =========================================================================
     * Step 1: Tách header và body từ frame plaintext
     * Nếu thiếu newline hoặc header quá dài, thông báo lỗi định dạng.
     * ========================================================================= */
    if (split_msg(plain, plen, header, sizeof header, &body, &blen) != 0 ||
        strlen(header) != (size_t)(body - plain - 1)) {
        say("[ERR] Frame khong hop le");
        return;
    }
    p = header;
    if (take_token(&p, action, sizeof action) != 1) {
        say("[ERR] Header rong");
        return;
    }

    /* =========================================================================
     * Step 2: Phân loại và hiển thị phản hồi
     * OK/ERR là ACK cho sender, MSG/FILE là dữ liệu gửi từ user khác.
     * ========================================================================= */
    if (strcmp(action, "OK") == 0) {
        if (blen || *p != ' ') { say("[ERR] OK sai dinh dang"); return; }
        say("[OK] %s", p + 1);
    } else if (strcmp(action, "ERR") == 0) {
        char code[16], *end;
        long n;
        if (take_token(&p, code, sizeof code) != 1 || *p != ' ') {
            say("[ERR] ERR sai dinh dang"); return;
        }
        n = strtol(code, &end, 10);
        if (blen || end == code || *end || n < 0 || n > 999) {
            say("[ERR] ERR sai dinh dang"); return;
        }
        say("[ERR %ld] %s", n, p + 1);
    } else if (strcmp(action, "MSG") == 0) {
        char from[MAX_USER_LEN + 1];
        if (take_token(&p, from, sizeof from) != 1 || *p != ' ' ||
            strpbrk(from, "/\\") != NULL || blen || !p[1]) {
            say("[ERR] MSG sai dinh dang"); return;
        }
        say("[MSG] %s: %s", from, p + 1);
    } else if (strcmp(action, "FILE") == 0) {
        char from[MAX_USER_LEN + 1], fname[MAX_FNAME_LEN + 1], sz[32];
        char path[sizeof(DOWNLOAD_DIR) + MAX_USER_LEN + MAX_FNAME_LEN + 8];
        unsigned long long expected;
        char *end;
        FILE *fp;
        int ok;
        if (take_token(&p, from, sizeof from) != 1 ||
            take_token(&p, fname, sizeof fname) != 1 ||
            take_token(&p, sz, sizeof sz) != 1 || !at_end(p) ||
            strpbrk(from, "/\\") || strpbrk(fname, "/\\") ||
            !from[0] || !fname[0]) {
            say("[ERR] FILE sai dinh dang"); return;
        }
        errno = 0;
        expected = strtoull(sz, &end, 10);
        if (errno || end == sz || *end || sz[0] == '-' ||
            expected == 0 || expected > MAX_FILE_SIZE || expected != blen) {
            say("[ERR] FILE size khong khop"); return;
        }

    /* =========================================================================
     * Step 3: Tạo thư mục downloads trước khi ghi ảnh
     * Nếu thư mục đã tồn tại (EEXIST), tiếp tục; lỗi khác thì không ghi.
     * ========================================================================= */
        if (mkdir(DOWNLOAD_DIR, 0755) != 0 && errno != EEXIST) {
            say("[ERR] Khong tao duoc downloads"); return;
        }
        snprintf(path, sizeof path, "%s/%s_%s", DOWNLOAD_DIR, from, fname);

    /* =========================================================================
     * Step 4: Mở file nhận theo chế độ binary
     * "wb" giúp bảo toàn mọi byte ảnh, kể cả byte 0x00.
     * ========================================================================= */
        fp = fopen(path, "wb");
        if (!fp) { say("[ERR] Khong mo duoc file de ghi"); return; }
        /* fwrite nhận đúng số byte, kể cả ký tự NUL 0x00 trong ảnh. */
        ok = fwrite(body, 1, blen, fp) == blen;
        if (fclose(fp) != 0) ok = 0;
        if (!ok) {
            unlink(path);
            say("[ERR] Khong ghi duoc file"); return;
        }
        say("[FILE] %s: %s (%zu bytes) -> %s", from, fname, blen, path);
    } else {
        say("[ERR] Header khong duoc ho tro");
    }
}

/* TODO 3: Luồng nhận (chạy nền suốt đời của chương trình).
 *   Lặp: recv_msg(g_fd, &plain, &plen)
 *     RECV_OK      -> handle_incoming(plain, plen); free(plain)
 *     RECV_BAD     -> say("[ERR] ...") rồi tiếp tục
 *     còn lại      -> say("[ERR] Mat ket noi toi server"); exit(1);
 *   Trả về NULL khi kết thúc (thực tế không tới được). */
/*
 * @brief Luồng nhận độc lập, không chặn luồng chính đọc stdin.
 * @input arg: không dùng, server đã kết nối tại g_fd.
 * @output NULL khi kết thúc logic; thoát mã 1 khi ngắt kết nối ngoài ý muốn.
 */
void *recv_thread(void *arg)
{
    (void)arg;

    /* =========================================================================
     * Step 1: Luồng nền liên tục nhận frame từ server
     * recv_msg gọi recv_all/recv; có thể blocking nhưng không chặn nhập stdin.
     * ========================================================================= */
    for (;;) {
        unsigned char *plain = NULL;
        size_t plen = 0;
        int rc = recv_msg(g_fd, &plain, &plen);

    /* =========================================================================
     * Step 2: Phân loại dữ liệu nhận được và xử lý lỗi
     * RECV_OK: xử lý và free; RECV_BAD: in lỗi và nhận tiếp; còn lại: thoát lỗi.
     * ========================================================================= */
        if (rc == RECV_OK) {
            handle_incoming(plain, plen);
            free(plain);
        } else if (rc == RECV_BAD) {
            say("[ERR] Frame ma hoa khong hop le");
        } else {
            say("[ERR] Mat ket noi toi server");
            exit(1);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Xử lý lệnh nhập từ bàn phím                                          */
/* ------------------------------------------------------------------ */

/* TODO 4: Xử lý một dòng lệnh đã bỏ '\n'. Trả 1 nếu cần thoát chương trình.
 *   Các lệnh (đề bài mục 5.2):
 *     /register <user> <pass>  -> gửi "REGISTER <user> <pass>"
 *     /login <user> <pass>     -> gửi "LOGIN <user> <pass>"
 *     /list                    -> gửi "LIST"
 *     /msg <user> <nội dung>   -> gửi "MSG <user> <nội dung>" (nội dung 1..1024 byte)
 *     /file <user> <đường_dẫn> -> đọc file ảnh và gửi "FILE <user> <tên> <size>" + dữ liệu
 *     /quit                    -> trả 1
 *   Kiểm tra cục bộ và báo "[ERR] ..." (không gửi gì) khi:
 *     - sai cú pháp (thiếu/thừa tham số, lệnh không bắt đầu bằng '/' hoặc không có trong danh sách)
 *     - /file: đuôi không thuộc png/jpg/jpeg/gif/bmp (không phân biệt hoa thường),
 *              không mở được file, file rỗng, file > 10 MiB, tên file > 128 byte
 *     - /msg: nội dung rỗng hoặc > 1024 byte
 *   Nếu send_cmd() lỗi: say("[ERR] Mat ket noi toi server") và trả 1. */
/*
 * @brief Kiểm tra và chuyển một dòng lệnh console thành frame giao thức.
 * @input line: chuỗi mutable, đã bỏ ký tự xuống dòng cuối.
 * @output 1 nếu /quit hoặc mất kết nối; 0 nếu còn tiếp tục.
 * @note Mọi lỗi cú pháp cục bộ chỉ in [ERR], tuyệt đối không gửi server.
 */
int handle_command(char *line)
{
    char action[32], user[MAX_USER_LEN + 1], pass[MAX_PASS_LEN + 1];
    char hdr[MAX_HEADER_LEN + 1];
    char *p = line;
    int rc;


    /* =========================================================================
     * Step 1: Kiểm tra lệnh nhập có mở đầu bằng dấu /
     * Sai cú pháp phải báo [ERR] tại client; không gửi frame.
     * ========================================================================= */
    if (line[0] != '/' || take_token(&p, action, sizeof action) != 1) {
        say("[ERR] Lenh phai bat dau bang /");
        return 0;
    }

    /* =========================================================================
     * Step 2: Điều hướng theo lệnh người dùng
     * /quit, /list, /register, /login, /msg, /file; không chấp nhận lệnh khác.
     * ========================================================================= */
    if (strcmp(action, "/quit") == 0) {
        if (!at_end(p)) { say("[ERR] /quit khong co tham so"); return 0; }
        /* Không gửi QUIT: đề yêu cầu /quit kết thúc Client với mã 0. */
        return 1;
    }
    if (strcmp(action, "/list") == 0) {
        if (!at_end(p)) { say("[ERR] /list khong co tham so"); return 0; }
        strcpy(hdr, "LIST");
    } else if (strcmp(action, "/register") == 0 ||
               strcmp(action, "/login") == 0) {
        if (take_token(&p, user, sizeof user) != 1 ||
            take_token(&p, pass, sizeof pass) != 1 || !at_end(p)) {
            say("[ERR] Cu phap: %s <user> <pass>", action); return 0;
        }
        snprintf(hdr, sizeof hdr, "%s %s %s",
                 strcmp(action, "/register") == 0 ? "REGISTER" : "LOGIN", user, pass);
    } else if (strcmp(action, "/msg") == 0) {
        size_t n;
        if (take_token(&p, user, sizeof user) != 1 || *p != ' ') {
            say("[ERR] Cu phap: /msg <user> <noi dung>"); return 0;
        }
        ++p;
        n = strlen(p);
        if (!n || n > MAX_TEXT_LEN) {
            say("[ERR] MSG phai co 1..1024 byte"); return 0;
        }
        snprintf(hdr, sizeof hdr, "MSG %s %s", user, p);
    } else if (strcmp(action, "/file") == 0) {
        char filepath[4096], *fname, *dot;
        FILE *fp;
        long n;
        unsigned char *data;
        size_t received;
        if (take_token(&p, user, sizeof user) != 1 ||
            take_token(&p, filepath, sizeof filepath) != 1 || !at_end(p)) {
            say("[ERR] Cu phap: /file <user> <duong_dan>"); return 0;
        }
        /* Lấy basename, đồng thời xử lý cả ký tự phân cách đường dẫn Windows. */
        fname = strrchr(filepath, '/');
        fname = fname ? fname + 1 : filepath;
        if (strrchr(fname, '\\')) fname = strrchr(fname, '\\') + 1;
        if (!fname[0] || strlen(fname) > MAX_FNAME_LEN) {
            say("[ERR] Ten file qua dai hoac rong"); return 0;
        }
        dot = strrchr(fname, '.');
        if (!dot || (strcasecmp(dot, ".png") && strcasecmp(dot, ".jpg") &&
                     strcasecmp(dot, ".jpeg") && strcasecmp(dot, ".gif") &&
                     strcasecmp(dot, ".bmp"))) {
            say("[ERR] Chi cho phep file anh png/jpg/jpeg/gif/bmp"); return 0;
        }

    /* =========================================================================
     * Step 3: Đọc dữ liệu file ảnh dạng nhị phân
     * Đuôi hợp lệ, kích thước 1..10MiB, đọc đủ trước khi gửi frame.
     * ========================================================================= */
        fp = fopen(filepath, "rb");
        if (!fp) { say("[ERR] Khong mo duoc file anh"); return 0; }
        if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) < 0 ||
            fseek(fp, 0, SEEK_SET) != 0) {
            fclose(fp); say("[ERR] Khong doc duoc kich thuoc file"); return 0;
        }
        if (n == 0 || (unsigned long)n > MAX_FILE_SIZE) {
            fclose(fp); say("[ERR] Kich thuoc file phai 1..10 MiB"); return 0;
        }
        data = malloc((size_t)n);
        if (!data) { fclose(fp); say("[ERR] Het bo nho"); return 0; }
        received = fread(data, 1, (size_t)n, fp);
        fclose(fp);
        if (received != (size_t)n) {
            free(data); say("[ERR] Khong doc du file"); return 0;
        }
        snprintf(hdr, sizeof hdr, "FILE %s %s %zu", user, fname, received);

    /* =========================================================================
     * Step 4: Gửi FILE kèm BODY ảnh đã đọc đủ
     * send_cmd đảm nhiệm mutex, đóng frame, mã hóa AES và gửi qua TCP.
     * ========================================================================= */
        rc = send_cmd(hdr, data, received);
        free(data);
        if (rc != 0) {
            say("[ERR] Mat ket noi toi server");
            return 1;
        }
        return 0;
    } else {
        say("[ERR] Lenh khong hop le");
        return 0;
    }

    /* =========================================================================
     * Step 5: Gửi các lệnh không có BODY
     * Đăng ký, đăng nhập, LIST, MSG chỉ cần HEADER+newline.
     * ========================================================================= */
    rc = send_cmd(hdr, NULL, 0);
    if (rc != 0) {
        say("[ERR] Mat ket noi toi server");
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* main (ĐÃ CUNG CẤP)                                                   */
/* ------------------------------------------------------------------ */

/*
 * @brief Kết nối TCP tới server và chạy luồng nhập/nhận song song.
 * @input argc, argv: IPAddress và PortNumber.
 * @output 0: thoát theo /quit; mã khác 0 nếu tham số/kết nối lỗi.
 * @detail main kết nối TCP, khởi động recv_thread, đọc lệnh từ stdin.
 */
int main(int argc, char **argv)
{
    struct sockaddr_in addr;
    pthread_t tid;
    char line[4096];
    long port;
    char *end;


    /* =========================================================================
     * Step 1: Kiểm tra cú pháp ./client IPAddress PortNumber
     * Thiếu/thừa tham số hoặc cổng ngoài 1..65535 sẽ báo lỗi và thoát.
     * ========================================================================= */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s IPAddress PortNumber\n", argv[0]);
        return 1;
    }
    port = strtol(argv[2], &end, 10);
    if (*end != '\0' || port < 1 || port > 65535) {
        say("[ERR] Cong khong hop le: %s", argv[2]);
        return 1;
    }


    /* =========================================================================
     * Step 2: Thiết lập địa chỉ IPv4 và chuyển byte order
     * sockaddr_in giữ IPv4+port; htons chuyển số cổng host -> network (big-endian).
     * ========================================================================= */
    /*
     * struct sockaddr_in: mô tả đích server cho connect().
     * AF_INET = IPv4; htons(port) = chuyển port 16 bit sang network byte order.
     * inet_pton(AF_INET, ip, &addr.sin_addr) = chuyển IPv4 dạng chữ sang nhị phân.
     * inet_pton trả 1 hợp lệ, 0 nếu địa chỉ sai, -1 nếu lỗi family.
     */
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
        say("[ERR] Dia chi IPv4 khong hop le: %s", argv[1]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    /* =========================================================================
     * Step 3: Tạo socket TCP IPv4
     * AF_INET: IPv4; SOCK_STREAM: TCP stream; 0: giao thức mặc định.
     * ========================================================================= */
    /*
     * socket(family, type, protocol): tạo endpoint giao tiếp mạng.
     * Input: AF_INET=IPv4, SOCK_STREAM=TCP, 0=mặc định.
     * Output: fd >= 0 thành công; -1 nếu lỗi tài nguyên/quyền.
     */
    g_fd = socket(AF_INET, SOCK_STREAM, 0);

    /* =========================================================================
     * Step 4: Kết nối tới IP/port server bằng TCP
     * connect thực hiện bắt tay TCP, trả 0 thành công và -1 nếu server không nghe.
     * ========================================================================= */
    /*
     * connect(fd, sockaddr*, addrlen): thiết lập TCP tới server.
     * Input: g_fd = socket client; &addr chứa IP/port; sizeof addr = số byte.
     * Output: 0 thành công; -1 nếu ECONNREFUSED, timeout hoặc lỗi socket.
     * Lưu ý toán tử ||: connect chỉ được gọi nếu socket() thành công.
     */
    if (g_fd < 0 || connect(g_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        say("[ERR] Khong ket noi duoc toi %s:%s", argv[1], argv[2]);
        return 1;
    }
    say("[INFO] Da ket noi toi %s:%s", argv[1], argv[2]);


    /* =========================================================================
     * Step 5: Tạo luồng nhận từ server
     * pthread_create chạy recv_thread độc lập; main vẫn có thể đọc stdin.
     * ========================================================================= */
    pthread_create(&tid, NULL, recv_thread, NULL);
    pthread_detach(tid);


    /* =========================================================================
     * Step 6: Luồng chính đọc lệnh từ bàn phím
     * Bỏ newline cuối chuỗi, gọi handle_command để gửi mã lệnh tương ứng.
     * ========================================================================= */
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n == 0)
            continue;
        if (handle_command(line))
            break;
    }

    /* =========================================================================
     * Step 7: Kết thúc chương trình client
     * /quit hoặc EOF stdin khiến main trả mã 0 theo yêu cầu.
     * ========================================================================= */
    return 0;
}
