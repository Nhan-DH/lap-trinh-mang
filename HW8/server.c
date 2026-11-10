/*
 * server.c - Server chat TCP đa luồng (BÀI LÀM CỦA SINH VIÊN)
 *
 * Cú pháp: ./server PortNumber
 *
 * Đã cung cấp sẵn: cấu trúc dữ liệu, tách token, kiểm tra hợp lệ, gửi
 * trả lời (reply_ok/reply_err), bộ phân phối lệnh handle_plain(), main().
 *
 * SINH VIÊN PHẢI HOÀN THIỆN các hàm đánh dấu TODO. Xem đề bài, mục 3 và 4.
 * Mọi truy cập danh sách online và account.txt phải nằm trong g_lock.
 * Mọi lần ghi lên socket phải nằm trong c->wlock (xem send_to).
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"

#define MAX_ONLINE 64

typedef struct conn {
    int fd;
    pthread_mutex_t wlock;          /* khóa ghi socket */
    char user[MAX_USER_LEN + 1];    /* rỗng nếu chưa đăng nhập */
} conn_t;

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
conn_t *g_online[MAX_ONLINE];        /* NULL = ô trống; bảo vệ bởi g_lock */

/* ------------------------------------------------------------------ */
/* Tiện ích (ĐÃ CUNG CẤP)                                               */
/* ------------------------------------------------------------------ */

/*
 * @brief Xác minh chuỗi username theo quy tắc giao thức.
 * @input s: chuỗi username do client gửi.
 * @output 1: hợp lệ; 0: không hợp lệ.
 * @detail Chỉ nhận chữ/số ASCII và dấu gạch dưới, độ dài 3..20 byte.
 */
int valid_user(const char *s)
{
    size_t n = strlen(s), i;

    if (n < MIN_USER_LEN || n > MAX_USER_LEN)
        return 0;
    for (i = 0; i < n; i++)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_'))
            return 0;
    return 1;
}

/*
 * @brief Kiểm tra độ dài mật khẩu theo yêu cầu bài.
 * @input s: chuỗi mật khẩu vừa tách từ header.
 * @output 1: từ 3 đến 32 byte; 0: không đạt.
 * @detail Mật khẩu đã được cắt token theo dấu cách; không nhận khoảng trắng.
 */
int valid_pass(const char *s)
{
    size_t n = strlen(s);
    return n >= MIN_PASS_LEN && n <= MAX_PASS_LEN;
}

/*
 * @brief Kiểm tra tên ảnh trước khi chuyển tiếp.
 * @input s: tên file không chứa đường dẫn.
 * @output 1: hợp lệ; 0: rỗng, quá dài hoặc có slash.
 * @detail Ngăn tên ảnh chứa / hoặc \ làm thành phần đường dẫn.
 */
int valid_fname(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > MAX_FNAME_LEN)
        return 0;
    return strchr(s, '/') == NULL && strchr(s, '\\') == NULL;
}

/* Lấy token tiếp theo (phân cách bởi khoảng trắng).
 * Trả 1 nếu có token, 0 nếu hết chuỗi, -1 nếu token dài hơn cap-1. */
/*
 * @brief Tách tham số phân cách bằng dấu cách trong header.
 * @input s: địa chỉ con trỏ đang duyệt; out: đích; cap: sức chứa.
 * @output 1: lấy được; 0: hết; -1: token quá dài.
 * @detail Cập nhật *s để các lần gọi tiếp theo đọc tham số kế tiếp.
 */
static int take_token(char **s, char *out, size_t cap)
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

/* Trả 1 nếu chỉ còn khoảng trắng (hoặc hết chuỗi). */
/*
 * @brief Kiểm tra sau các tham số có còn dữ liệu thừa.
 * @input s: phần header còn lại sau khi tách token.
 * @output 1: chỉ còn khoảng trắng; 0: còn ký tự khác.
 * @detail Giúp từ chối REGISTER/LOGIN có quá nhiều tham số.
 */
int at_end(char *s)
{
    while (*s == ' ')
        s++;
    return *s == '\0';
}

/* ------------------------------------------------------------------ */
/* Gửi dữ liệu                                                          */
/* ------------------------------------------------------------------ */

/*
 * @brief Gửi trọn một frame mã hóa qua socket của client.
 * @input c: con trỏ kết nối hợp lệ; hdr: dòng lệnh/phản hồi;
 *        body: bytes nhị phân hoặc NULL; blen: số byte của body.
 * @output 0 nếu gửi đủ; -1 nếu lỗi socket/mã hóa/bộ nhớ.
 * @thread Mọi luồng có thể gửi đến c; mutex wlock bảo vệ cả frame.
 */
/* TODO 1: Gửi một frame (header, body) lên socket của c.
 *   - Phải khóa c->wlock trước khi gọi send_msg() và mở khóa sau đó.
 *   - Trả về giá trị của send_msg(): 0 nếu thành công, -1 nếu lỗi.
 *   Lý do: nhiều luồng có thể cùng ghi lên một socket (chuyển tiếp MSG/FILE
 *   từ người khác). Nếu không khóa, frame sẽ bị xen kẽ và hỏng. */
int send_to(conn_t *c, const char *hdr, const void *body, size_t blen)
{
    /* Mỗi frame phải được gửi liền mạch: không để hai pthread ghi xen kẽ. */
    int result;
    /* =========================================================================
     * Step 1: Khóa socket để tránh trộn frame
     * Hai luồng có thể cùng gửi tới một người nhận; giữ mutex quanh toàn bộ send_msg().
     * ========================================================================= */

    pthread_mutex_lock(&c->wlock);
    result = send_msg(c->fd, hdr, body, blen);

    /* =========================================================================
     * Step 2: Mở khóa và trả trạng thái gửi
     * send_msg: 0 thành công, -1 nếu lỗi mã hóa, cấp phát hoặc TCP send().
     * ========================================================================= */
    pthread_mutex_unlock(&c->wlock);
    return result;
}

/*
 * @brief Gửi phản hồi thành công theo giao thức OK.
 * @input c: socket người yêu cầu; text: phần nội dung sau OK.
 * @output Không trả giá trị; đóng gói và gửi thông qua send_to().
 * @detail send_to() phụ trách mã hóa và mutex ghi socket.
 */
static void reply_ok(conn_t *c, const char *text)
{
    char hdr[MAX_HEADER_LEN + 1];

    snprintf(hdr, sizeof hdr, "OK %s", text);
    send_to(c, hdr, NULL, 0);
}

/*
 * @brief Gửi mã lỗi theo cú pháp ERR <code> <message>.
 * @input c: kết nối; code: mã số; text: mô tả.
 * @output Không trả giá trị; client tự in [ERR code].
 * @detail Chỉ phản hồi cho client gửi yêu cầu, không gửi cả phòng.
 */
static void reply_err(conn_t *c, int code, const char *text)
{
    char hdr[MAX_HEADER_LEN + 1];

    snprintf(hdr, sizeof hdr, "ERR %d %s", code, text);
    send_to(c, hdr, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* File account.txt và danh sách online (gọi khi ĐANG giữ g_lock)       */
/* ------------------------------------------------------------------ */

/*
 * @brief Tra cứu username trong file account.txt đã có sẵn.
 * @input user: username cần tìm; hash_out: mảng 65 byte nhận SHA-256 hex.
 * @output 1 nếu tìm thấy (hash_out hợp lệ); 0 nếu không thấy/chưa có file.
 * @pre Caller đang giữ g_lock; không gọi khi không giữ mutex.
 */
/* TODO 2: Tìm user trong ACCOUNT_FILE ("account.txt").
 *   Mỗi dòng có dạng "<username> <sha256_hex>".
 *   Trả 1 nếu tìm thấy và ghi hash vào hash_out[65]; trả 0 nếu không có
 *   (kể cả khi file chưa tồn tại). Dùng sha256_hex() ở crypto.c để băm. */
int account_find(const char *user, char hash_out[65])
{

    /* =========================================================================
     * Step 1: Mở file account.txt để tìm username
     * fopen(..., "r") chỉ đọc; NULL nếu chưa có file hoặc không đọc được.
     * ========================================================================= */
    FILE *fp = fopen(ACCOUNT_FILE, "r");
    char line[256], name[MAX_USER_LEN + 1], hash[65];

    /* Chưa có account.txt là bình thường khi chưa có ai đăng ký. */
    if (!fp)
        return 0;

    /* =========================================================================
     * Step 2: Duyệt từng bản ghi
     * Mỗi dòng được chấm theo dạng username + dấu cách + 64 ký tự SHA-256.
     * ========================================================================= */
    while (fgets(line, sizeof line, fp)) {
        /* Chỉ đọc hai cột: username và SHA-256 hex (64 ký tự). */
        if (sscanf(line, "%20s %64s", name, hash) == 2 &&
            strcmp(name, user) == 0 && strlen(hash) == 64) {
            memcpy(hash_out, hash, 65);
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

/*
 * @brief Thêm tài khoản vào cuối account.txt.
 * @input user: username đúng định dạng; hash: SHA-256 hex có 64 ký tự.
 * @output 0 nếu ghi/đóng thành công; -1 nếu lỗi I/O.
 * @pre Caller đang giữ g_lock sau khi kiểm tra chưa có username.
 */
/* TODO 3: Ghi thêm một dòng "<user> <hash>" vào cuối ACCOUNT_FILE.
 *   Trả 0 nếu thành công, -1 nếu lỗi. */
int account_add(const char *user, const char hash[65])
{

    /* =========================================================================
     * Step 1: Mở account.txt theo chế độ nối cuối
     * Nếu chưa tồn tại sẽ tạo file; trả NULL nếu quyền ghi hoặc đĩa lỗi.
     * ========================================================================= */
    FILE *fp = fopen(ACCOUNT_FILE, "a");
    int ok;
    if (!fp)
        return -1;
    /* Ghi một dòng hoàn chỉnh; fflush/fclose để phát hiện lỗi ghi đĩa. */

    /* =========================================================================
     * Step 2: Ghi nguyên dòng và kiểm tra I/O
     * fflush/fclose giúp phát hiện lỗi ghi đĩa trước khi báo thành công.
     * ========================================================================= */
    ok = fprintf(fp, "%s %s\n", user, hash) > 0;
    if (fflush(fp) != 0)
        ok = 0;
    if (fclose(fp) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

/*
 * @brief Tra cứu kết nối online theo username.
 * @input user: tên tài khoản cần tìm.
 * @output conn_t* trong mảng g_online hoặc NULL.
 * @pre Caller giữ g_lock; không sử dụng con trỏ sau khi mở khóa nếu
 *      chưa hoàn tất thao tác cần thiết.
 */
/* TODO 4: Tìm kết nối đang đăng nhập với username `user` trong g_online.
 *   Trả con trỏ conn_t* hoặc NULL. */
conn_t *online_find(const char *user)
{
    size_t i;

    /* =========================================================================
     * Step 1: Tìm trong danh sách người dùng đang online
     * Caller phải giữ g_lock để con trỏ không bị pthread khác giải phóng.
     * ========================================================================= */
    for (i = 0; i < MAX_ONLINE; ++i)
        if (g_online[i] && strcmp(g_online[i]->user, user) == 0)
            return g_online[i];
    return NULL;
}

/*
 * @brief Đưa một kết nối vừa LOGIN vào ô đầu tiên còn trống.
 * @input c: con trỏ sở hữu bởi worker pthread, user đã được gán.
 * @output 0 nếu có chỗ; -1 nếu đạt MAX_ONLINE.
 * @pre Caller giữ g_lock.
 */
/* TODO 5: Thêm c vào g_online (vào ô NULL đầu tiên). Trả 0 nếu OK, -1 nếu đầy. */
int online_add(conn_t *c)
{
    size_t i;

    /* =========================================================================
     * Step 1: Tìm ô trống trong bảng online
     * Một ô NULL là chưa có user đăng nhập; bảo vệ bằng g_lock.
     * ========================================================================= */
    for (i = 0; i < MAX_ONLINE; ++i) {
        if (!g_online[i]) {
            g_online[i] = c;
            return 0;
        }
    }
    return -1;
}

/*
 * @brief Gỡ kết nối đã thoát khỏi danh sách online.
 * @input c: con trỏ kết nối đang bị dọn dẹp.
 * @output Không trả giá trị; g_online không còn giữ c.
 * @pre Caller giữ g_lock, gọi trước khi free(c).
 */
/* TODO 6: Gỡ c khỏi g_online (nếu có). Không làm gì nếu c không có trong bảng. */
void online_remove(conn_t *c)
{
    size_t i;

    /* =========================================================================
     * Step 1: Bỏ tham chiếu trước khi giải phóng conn_t
     * Tránh luồng khác tìm thấy con trỏ đã free và ghi socket không còn sống.
     * ========================================================================= */
    for (i = 0; i < MAX_ONLINE; ++i) {
        if (g_online[i] == c) {
            g_online[i] = NULL;
            return;
        }
    }
}

/*
 * @brief Kiểm tra header có phải UTF-8 hợp lệ hay không.
 * @input p: buffer header; n: số byte (không nhất thiết có NUL cuối).
 * @output 1 nếu hợp lệ; 0 nếu byte UTF-8 sai, sequence quá ngắn,
 *         surrogate, overlong, hoặc codepoint lớn hơn U+10FFFF.
 * @note Body ảnh có thể chứa mọi byte; tuyệt đối không xác minh UTF-8 ở body.
 */
/*
 * @brief Duyệt từng byte để kiểm tra header UTF-8 hợp lệ.
 * @input p: dữ liệu header; n: số byte header.
 * @output 1: UTF-8 hợp lệ; 0: chuỗi byte không hợp lệ.
 * @detail Chỉ kiểm tra HEADER; ảnh binary trong BODY không bị kiểm tra.
 */
static int valid_utf8(const unsigned char *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        size_t count;
        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF) count = 2;
        else if (c >= 0xE0 && c <= 0xEF) count = 3;
        else if (c >= 0xF0 && c <= 0xF4) count = 4;
        else return 0;
        if (n - i < count) return 0;
        for (size_t k = 1; k < count; k++)
            if ((p[i+k] & 0xC0) != 0x80) return 0;
        if (count == 3 && ((c == 0xE0 && p[i+1] < 0xA0) ||
                           (c == 0xED && p[i+1] >= 0xA0))) return 0;
        if (count == 4 && ((c == 0xF0 && p[i+1] < 0x90) ||
                           (c == 0xF4 && p[i+1] >= 0x90))) return 0;
        i += count;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Xử lý từng lệnh                                                      */
/* ------------------------------------------------------------------ */

/* TODO 7: REGISTER <user> <pass>   (đề bài mục 3.2, 4)
 *   - Nếu c đã đăng nhập: ERR 403.
 *   - Kiểm tra cú pháp: đúng 2 tham số, !at_end sau đó, valid_user, valid_pass.
 *     Sai: ERR 400 (dùng take_token/at_end/valid_user/valid_pass như handle_plain).
 *   - Băm mật khẩu bằng sha256_hex(); khóa g_lock; nếu user đã có: ERR 409;
 *     nếu không: account_add() rồi OK. Mở khóa g_lock trước khi reply_*. */
/*
 * @brief Xử lý REGISTER và ghi tài khoản mới vào account.txt.
 * @input c: kết nối yêu cầu; p: phần header sau từ REGISTER.
 * @output Gửi OK hoặc ERR 403/400/409/500; không thay trạng thái online.
 * @mutex g_lock giữ liên tục từ bước tìm user đến ghi file (atomic logic).
 */
void cmd_register(conn_t *c, char *p)
{
    char user[MAX_USER_LEN + 1], pass[MAX_PASS_LEN + 1], old_hash[65];
    char new_hash[65];
    int status;


    /* =========================================================================
     * Step 1: Kiểm tra trạng thái kết nối
     * REGISTER chỉ cho phép khi kết nối chưa LOGIN; nếu đã login trả ERR 403.
     * ========================================================================= */
    if (c->user[0]) {
        reply_err(c, ERR_FORBIDDEN, "Da dang nhap");
        return;
    }

    /* =========================================================================
     * Step 2: Phân tích và xác thực dữ liệu đầu vào
     * Hai tham số user/pass bắt buộc; username hợp lệ, password 3..32 byte.
     * ========================================================================= */
    if (take_token(&p, user, sizeof user) != 1 ||
        take_token(&p, pass, sizeof pass) != 1 ||
        !at_end(p) || !valid_user(user) || !valid_pass(pass)) {
        reply_err(c, ERR_BAD_REQUEST, "REGISTER khong hop le");
        return;
    }

    /* =========================================================================
     * Step 3: Tạo SHA-256 hex của mật khẩu theo ĐỀ BÀI
     * Bài tập yêu cầu account.txt lưu SHA-256 hex; không thay bằng bcrypt ở bài này.
     * ========================================================================= */
    if (sha256_hex((const unsigned char *)pass, strlen(pass), new_hash) != 0) {
        reply_err(c, ERR_INTERNAL, "Loi bam mat khau");
        return;
    }


    /* =========================================================================
     * Step 4: Vùng găng: kiểm tra trùng và ghi tài khoản
     * Giữ cùng mutex từ account_find tới account_add để 10 client đăng ký trùng chỉ 1 thành công.
     * ========================================================================= */
    pthread_mutex_lock(&g_lock);
    if (account_find(user, old_hash))
        status = ERR_CONFLICT;
    else if (account_add(user, new_hash) != 0)
        status = ERR_INTERNAL;
    else
        status = 0;
    pthread_mutex_unlock(&g_lock);


    /* =========================================================================
     * Step 5: Gửi phản hồi sau khi đã mở khóa
     * Tránh giữ g_lock khi đang gửi phản hồi qua TCP.
     * ========================================================================= */
    if (status == ERR_CONFLICT)
        reply_err(c, status, "Username da ton tai");
    else if (status == ERR_INTERNAL)
        reply_err(c, status, "Khong ghi duoc account.txt");
    else
        reply_ok(c, "Dang ky thanh cong");
}

/* TODO 8: LOGIN <user> <pass>   (đề bài mục 3.2, 4)
 *   - Nếu c đã đăng nhập: ERR 403.
 *   - Sai cú pháp: ERR 400.
 *   - Khóa g_lock. Không có user hoặc sai hash: ERR 401.
 *   - Nếu user đang online ở kết nối khác (online_find != NULL): ERR 409.
 *   - Nếu được: gán c->user rồi online_add(c); OK. Đảm bảo gán c->user
 *     xảy ra TRONG g_lock để luồng khác đọc đúng. */
/*
 * @brief Xác thực LOGIN và liên kết username với TCP connection.
 * @input c: kết nối; p: chuỗi "<user> <pass>".
 * @output OK, hoặc ERR 403/400/401/409/500.
 * @note Đây là trạng thái online theo socket, KHÔNG phải JWT/Session Token.
 */
void cmd_login(conn_t *c, char *p)
{
    char user[MAX_USER_LEN + 1], pass[MAX_PASS_LEN + 1];
    char stored[65], computed[65];
    int status;


    /* =========================================================================
     * Step 1: Chặn LOGIN lặp trên cùng kết nối
     * Nếu username đã gắn vào conn_t thì trả ERR 403.
     * ========================================================================= */
    if (c->user[0]) {
        reply_err(c, ERR_FORBIDDEN, "Da dang nhap");
        return;
    }

    /* =========================================================================
     * Step 2: Phân tích cú pháp USER/PASS
     * Kiểm tra đúng hai tham số và không có tham số thừa.
     * ========================================================================= */
    if (take_token(&p, user, sizeof user) != 1 ||
        take_token(&p, pass, sizeof pass) != 1 ||
        !at_end(p) || !valid_user(user) || !valid_pass(pass)) {
        reply_err(c, ERR_BAD_REQUEST, "LOGIN khong hop le");
        return;
    }

    /* =========================================================================
     * Step 3: Băm mật khẩu đăng nhập để so với file
     * Chỉ lưu hash trong account.txt, không lưu mật khẩu plaintext.
     * ========================================================================= */
    if (sha256_hex((const unsigned char *)pass, strlen(pass), computed) != 0) {
        reply_err(c, ERR_INTERNAL, "Loi bam mat khau");
        return;
    }


    /* =========================================================================
     * Step 4: Xác thực và kiểm tra một phiên online/user
     * Trong cùng g_lock: tìm tài khoản, kiểm tra hash, chặn login cùng user ở socket khác.
     * ========================================================================= */
    pthread_mutex_lock(&g_lock);
    if (!account_find(user, stored) || strcmp(stored, computed) != 0)
        status = ERR_BAD_CRED;
    else if (online_find(user))
        status = ERR_CONFLICT;
    else {
        /* Gán username trước khi công bố con trỏ trong danh sách online. */
        strcpy(c->user, user);
        if (online_add(c) != 0) {
            c->user[0] = '\0';
            status = ERR_INTERNAL;
        } else
            status = 0;
    }
    pthread_mutex_unlock(&g_lock);


    /* =========================================================================
     * Step 5: Gửi OK hoặc mã lỗi phù hợp
     * ERR 401 cho sai tài khoản/mật khẩu, ERR 409 cho user đã online.
     * ========================================================================= */
    if (status == ERR_BAD_CRED)
        reply_err(c, status, "Tai khoan hoac mat khau sai");
    else if (status == ERR_CONFLICT)
        reply_err(c, status, "Tai khoan dang online");
    else if (status == ERR_INTERNAL)
        reply_err(c, status, "Danh sach online da day");
    else
        reply_ok(c, "Dang nhap thanh cong");
}

/* TODO 9: LIST   (đề bài mục 3.2)
 *   - Chưa đăng nhập: ERR 403.
 *   - Trả "OK USERS <u1> <u2> ..." gồm mọi user đang online (kể cả mình).
 *   - Đọc g_online trong g_lock, dựng chuỗi trong buffer, rồi reply_ok. */
/*
 * @brief Lấy danh sách online một cách nhất quán.
 * @input c: kết nối gửi LIST.
 * @output "OK USERS <u1> ..."; ERR 403 nếu chưa LOGIN.
 */
void cmd_list(conn_t *c)
{
    char text[1500]; /* 64 online * (1 + 20) <= 1344; tránh tràn reply_ok */
    size_t used = (size_t)snprintf(text, sizeof text, "USERS");
    size_t i;

    /* =========================================================================
     * Step 1: Kiểm tra đã đăng nhập
     * Không cho client chưa xác thực lấy danh sách online.
     * ========================================================================= */
    if (!c->user[0]) {
        reply_err(c, ERR_FORBIDDEN, "Can dang nhap");
        return;
    }

    /* =========================================================================
     * Step 2: Đọc snapshot danh sách online dưới mutex
     * Chỉ giữ lock khi dựng chuỗi, gửi sau khi đã unlock để giảm thời gian khóa.
     * ========================================================================= */
    pthread_mutex_lock(&g_lock);
    for (i = 0; i < MAX_ONLINE; ++i) {
        if (g_online[i]) {
            int n = snprintf(text + used, sizeof text - used,
                             " %s", g_online[i]->user);
            if (n < 0 || (size_t)n >= sizeof text - used)
                break;
            used += (size_t)n;
        }
    }
    pthread_mutex_unlock(&g_lock);

    /* =========================================================================
     * Step 3: Gửi danh sách theo chuẩn OK USERS ...
     * Bao gồm chính user yêu cầu và những user khác đang online.
     * ========================================================================= */
    reply_ok(c, text);
}

/* TODO 10: MSG <user> <nội dung>   (đề bài mục 3.2, 4)
 *   - Chưa đăng nhập: ERR 403. Sai cú pháp / user sai định dạng: ERR 400.
 *   - Nội dung là phần còn lại của dòng (có khoảng trắng), 1..1024 byte.
 *     Rỗng hoặc quá dài: ERR 400.
 *   - Khóa g_lock; tìm người nhận; không online: ERR 404.
 *   - Gửi cho người nhận frame "MSG <người_gửi> <nội dung>" (send_to),
 *     rồi trả "OK SENT" cho người gửi. Lỗi gửi: ERR 500. */
/*
 * @brief Chuyển tiếp đúng một tin nhắn văn bản tới người nhận đang online.
 * @input c: người gửi; p: "<recipient> <text UTF-8>".
 * @output Sender: OK SENT/ERR; recipient: MSG <sender> <text>.
 * @mutex g_lock giữ con trỏ recipient sống đến khi send_to kết thúc;
 *        send_to tự khóa wlock của socket đích để không xen kẽ frame.
 */
void cmd_msg(conn_t *c, char *p)
{
    char user[MAX_USER_LEN + 1];
    char hdr[MAX_HEADER_LEN + 1];
    size_t len;
    conn_t *recipient;
    int status;


    /* =========================================================================
     * Step 1: Authentication: yêu cầu LOGIN trước
     * Nếu chưa gắn username vào socket, trả ERR 403.
     * ========================================================================= */
    if (!c->user[0]) {
        reply_err(c, ERR_FORBIDDEN, "Can dang nhap");
        return;
    }

    /* =========================================================================
     * Step 2: Tách tên người nhận và nội dung
     * Giữ nguyên các dấu cách trong nội dung tin nhắn; giới hạn 1..1024 byte.
     * ========================================================================= */
    if (take_token(&p, user, sizeof user) != 1 || !valid_user(user) ||
        *p != ' ') {
        reply_err(c, ERR_BAD_REQUEST, "MSG sai cu phap");
        return;
    }
    ++p;  /* Bỏ đúng một dấu cách phân tách, bảo toàn phần nội dung còn lại. */
    len = strlen(p);
    if (len == 0 || len > MAX_TEXT_LEN) {
        reply_err(c, ERR_BAD_REQUEST, "Noi dung MSG khong hop le");
        return;
    }
    snprintf(hdr, sizeof hdr, "MSG %s %s", c->user, p);


    /* =========================================================================
     * Step 3: Tra cứu người nhận và chuyển tiếp an toàn
     * Giữ g_lock để conn_t đích không bị free; send_to khóa riêng wlock của đích.
     * ========================================================================= */
    pthread_mutex_lock(&g_lock);
    recipient = online_find(user);
    if (!recipient)
        status = ERR_NOT_FOUND;
    else
        status = send_to(recipient, hdr, NULL, 0) == 0 ? 0 : ERR_INTERNAL;
    pthread_mutex_unlock(&g_lock);


    /* =========================================================================
     * Step 4: Trả kết quả cho người gửi
     * OK SENT sau khi chuyển tiếp; ERR 404 nếu người nhận không online.
     * ========================================================================= */
    if (status == ERR_NOT_FOUND)
        reply_err(c, status, "Nguoi nhan khong online");
    else if (status != 0)
        reply_err(c, status, "Khong gui duoc tin nhan");
    else
        reply_ok(c, "SENT");
}

/* TODO 11: FILE <user> <tên_file> <size>\n<dữ liệu>   (đề bài mục 3.2, 4)
 *   - Chưa đăng nhập: ERR 403.
 *   - Sai cú pháp; tên file sai (valid_fname); size không phải số hoặc = 0: ERR 400.
 *   - size > MAX_FILE_SIZE: ERR 413.
 *   - blen (độ dài phần thân thực tế) phải bằng size, nếu không: ERR 400.
 *   - Không online: ERR 404. Nếu được: gửi cho người nhận
 *     "FILE <người_gửi> <tên_file> <size>" + body; rồi OK SENT.
 *   Lưu ý: dữ liệu phần thân đã được đọc đầy đủ trước khi vào hàm này. */
/*
 * @brief Chuyển tiếp file ảnh nhị phân, không thay đổi byte nội dung.
 * @input p: "<recipient> <filename> <size>"; body/blen: bytes ảnh gốc.
 * @output Sender: OK SENT/ERR; recipient: FILE ... + đúng body bytes.
 * @note Giới hạn 10 MiB; không dùng strlen(body) vì ảnh có byte 0x00.
 */
void cmd_file(conn_t *c, char *p, const unsigned char *body, size_t blen)
{
    char user[MAX_USER_LEN + 1], fname[MAX_FNAME_LEN + 1], sz[32];
    char hdr[MAX_HEADER_LEN + 1];
    conn_t *recipient;
    unsigned long long n;
    char *end;
    int status;


    /* =========================================================================
     * Step 1: Authentication: chỉ gửi file sau LOGIN
     * FILE từ kết nối chưa đăng nhập luôn bị ERR 403.
     * ========================================================================= */
    if (!c->user[0]) {
        reply_err(c, ERR_FORBIDDEN, "Can dang nhap");
        return;
    }

    /* =========================================================================
     * Step 2: Kiểm tra cú pháp FILE recipient filename size
     * Tên file không có dấu / hoặc \ nhằm tránh đường dẫn bất hợp lệ.
     * ========================================================================= */
    if (take_token(&p, user, sizeof user) != 1 ||
        take_token(&p, fname, sizeof fname) != 1 ||
        take_token(&p, sz, sizeof sz) != 1 ||
        !at_end(p) || !valid_user(user) || !valid_fname(fname)) {
        reply_err(c, ERR_BAD_REQUEST, "FILE sai cu phap");
        return;
    }

    /* =========================================================================
     * Step 3: Kiểm tra kích thước dưới dạng số nguyên
     * Từ chối 0, sai cú pháp, >10MiB và size khác số byte BODY thực nhận.
     * ========================================================================= */
    errno = 0;
    n = strtoull(sz, &end, 10);
    if (errno || end == sz || *end || sz[0] == '-' || n == 0) {
        reply_err(c, ERR_BAD_REQUEST, "Size FILE khong hop le");
        return;
    }
    if (n > MAX_FILE_SIZE) {
        reply_err(c, ERR_TOO_LARGE, "File vuot 10 MiB");
        return;
    }
    if ((size_t)n != blen) {
        reply_err(c, ERR_BAD_REQUEST, "Size FILE khong khop body");
        return;
    }
    snprintf(hdr, sizeof hdr, "FILE %s %s %zu", c->user, fname, blen);


    /* =========================================================================
     * Step 4: Chuyển tiếp nguyên dữ liệu ảnh tới người nhận
     * Dùng độ dài blen, không dùng strlen vì dữ liệu ảnh có thể chứa byte 0x00.
     * ========================================================================= */
    pthread_mutex_lock(&g_lock);
    recipient = online_find(user);
    if (!recipient)
        status = ERR_NOT_FOUND;
    else
        status = send_to(recipient, hdr, body, blen) == 0 ? 0 : ERR_INTERNAL;
    pthread_mutex_unlock(&g_lock);


    /* =========================================================================
     * Step 5: Gửi ACK hoặc thông báo lỗi
     * Phản hồi gửi cho sender; FILE đích không nhận thêm ACK.
     * ========================================================================= */
    if (status == ERR_NOT_FOUND)
        reply_err(c, status, "Nguoi nhan khong online");
    else if (status != 0)
        reply_err(c, status, "Khong gui duoc file");
    else
        reply_ok(c, "SENT");
}

/* Xử lý một frame đã giải mã (ĐÃ CUNG CẤP). Trả 1 nếu cần đóng kết nối. */
/*
 * @brief Phân phối một frame plaintext tới hàm xử lý theo action.
 * @input c: kết nối; plain: buffer đã giải mã; plen: độ dài.
 * @output 1: yêu cầu QUIT, cần đóng socket; 0: tiếp tục.
 * @detail Tách HEADER/BODY, kiểm tra cú pháp và gọi cmd_register/cmd_login/...
 */
int handle_plain(conn_t *c, unsigned char *plain, size_t plen)
{
    char header[MAX_HEADER_LEN + 1], cmd[16];
    const unsigned char *body = NULL;
    size_t blen = 0;
    char *p;


    /* =========================================================================
     * Step 1: Tách HEADER và BODY từ frame plaintext
     * split_msg tìm byte xuống dòng đầu tiên; nếu thiếu newline trả ERR 400.
     * ========================================================================= */
    if (split_msg(plain, plen, header, sizeof header, &body, &blen) != 0) {
        reply_err(c, ERR_BAD_REQUEST, "Thieu xuong dong hoac dong lenh qua dai");
        return 0;
    }
    /* Không chấp nhận header có NUL nhúng hoặc dữ liệu không phải UTF-8.
       Với file nhị phân, chỉ kiểm tra HEADER, không kiểm tra BODY. */

    /* =========================================================================
     * Step 2: Kiểm tra header UTF-8 và dữ liệu NUL
     * Ngăn xử lý header bị ngắt bởi NUL; body ảnh là binary nên bỏ qua.
     * ========================================================================= */
    if (strlen(header) != (size_t)(body - plain - 1) ||
        !valid_utf8((const unsigned char *)header, strlen(header))) {
        reply_err(c, ERR_BAD_REQUEST, "Header khong hop le");
        return 0;
    }
    p = header;
    if (take_token(&p, cmd, sizeof cmd) != 1) {
        reply_err(c, ERR_BAD_REQUEST, "Lenh rong");
        return 0;
    }

    /* Chỉ FILE được mang dữ liệu nhị phân trong BODY. */
    if (strcmp(cmd, "FILE") != 0 && blen != 0) {
        reply_err(c, ERR_BAD_REQUEST, "BODY khong duoc phep");
        return 0;
    }
    if (strcmp(cmd, "LIST") == 0 && !at_end(p)) {
        reply_err(c, ERR_BAD_REQUEST, "LIST sai cu phap");
        return 0;
    }
    if (strcmp(cmd, "QUIT") == 0 && !at_end(p)) {
        reply_err(c, ERR_BAD_REQUEST, "QUIT sai cu phap");
        return 0;
    }

    /* =========================================================================
     * Step 3: Điều hướng đến các handler theo tên lệnh
     * Các action phân biệt hoa thường; chỉ FILE được mang BODY nhị phân.
     * ========================================================================= */
    if (strcmp(cmd, "REGISTER") == 0)
        cmd_register(c, p);
    else if (strcmp(cmd, "LOGIN") == 0)
        cmd_login(c, p);
    else if (strcmp(cmd, "LIST") == 0)
        cmd_list(c);
    else if (strcmp(cmd, "MSG") == 0)
        cmd_msg(c, p);
    else if (strcmp(cmd, "FILE") == 0)
        cmd_file(c, p, body, blen);
    else if (strcmp(cmd, "QUIT") == 0) {
        reply_ok(c, "BYE");
        return 1;
    } else
        reply_err(c, ERR_BAD_REQUEST, "Lenh khong hop le");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Luồng phục vụ một kết nối                                            */
/* ------------------------------------------------------------------ */

/* TODO 12: Vòng lặp phục vụ một kết nối (chạy trong pthread riêng).
 *   - Lặp: recv_msg() rồi:
 *       RECV_IO       -> thoát vòng lặp (client đóng/lỗi mạng)
 *       RECV_TOO_BIG  -> reply_err(413), thoát vòng lặp
 *       RECV_BAD      -> reply_err(400), tiếp tục
 *       RECV_OK       -> handle_plain(); free(plain); nếu trả 1 thì thoát
 *   - Khi thoát: gỡ c khỏi g_online (khóa g_lock), đóng socket,
 *     destroy wlock, free(c). Server KHÔNG được dừng vì một client lỗi. */
/*
 * @brief Worker pthread phục vụ riêng một TCP client đến khi QUIT/EOF/lỗi.
 * @input arg: conn_t* do main cấp phát bằng calloc.
 * @output NULL; gỡ khỏi online, close socket, destroy mutex và free bộ nhớ.
 * @note Mỗi frame giải mã được free chính xác một lần, kể cả khi QUIT.
 */
void *conn_thread(void *arg)
{
    conn_t *c = arg;

    /* =========================================================================
     * Step 1: Lặp nhận frame của một Client
     * recv_msg đọc LEN + IV + ciphertext và giải mã, có thể block ở luồng riêng.
     * ========================================================================= */
    for (;;) {
        unsigned char *plain = NULL;
        size_t plen = 0;
        int rc = recv_msg(c->fd, &plain, &plen);
        int stop = 0;


    /* =========================================================================
     * Step 2: Phân loại kết quả nhận và giải mã
     * RECV_IO: EOF/lỗi; TOO_BIG: 413+đóng; BAD: 400+tiếp tục; OK: xử lý.
     * ========================================================================= */
        if (rc == RECV_IO)
            break;
        if (rc == RECV_TOO_BIG) {
            reply_err(c, ERR_TOO_LARGE, "Frame vuot 12 MiB");
            break;  /* Không thể đồng bộ lại luồng nếu chưa đọc payload. */
        }
        if (rc == RECV_BAD) {
            reply_err(c, ERR_BAD_REQUEST, "Frame ma hoa khong hop le");
            continue;  /* Đã đọc hết frame; frame kế tiếp vẫn có ranh giới. */
        }
        if (rc == RECV_OK) {
            stop = handle_plain(c, plain, plen);
            free(plain);
            if (stop)
                break;
        }
    }

    /* Gỡ con trỏ dưới g_lock trước khi close/free để luồng khác không gửi
       vào socket hoặc con trỏ đã bị hủy. */

    /* =========================================================================
     * Step 3: Dọn kết nối sau QUIT, EOF hoặc lỗi
     * Gỡ online dưới mutex rồi mới close/free; tránh use-after-free.
     * ========================================================================= */
    pthread_mutex_lock(&g_lock);
    online_remove(c);
    c->user[0] = '\0';
    pthread_mutex_unlock(&g_lock);
    /*
     * shutdown(fd, SHUT_RDWR): ngừng cả hai chiều đọc/ghi TCP trước close.
     *   Input: fd = socket của client, SHUT_RDWR = cả RX và TX.
     *   Output: 0 nếu thành công, -1 khi socket đã đóng/không hợp lệ.
     * close(fd): giải phóng descriptor; 0 thành công, -1 lỗi.
     * Socket vẫn phải được close kể cả khi shutdown thất bại.
     */
    shutdown(c->fd, SHUT_RDWR);
    close(c->fd);
    pthread_mutex_destroy(&c->wlock);
    free(c);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* main (ĐÃ CUNG CẤP)                                                   */
/* ------------------------------------------------------------------ */

/*
 * @brief Khởi động TCP server và tạo một pthread cho mỗi client.
 * @input argc, argv: chương trình và PortNumber.
 * @output 1: lỗi khởi tạo; trong trường hợp thường, accept chạy liên tục.
 * @detail Dùng IPv4 TCP: socket -> bind -> listen -> accept -> pthread_create.
 */
int main(int argc, char **argv)
{
    int lfd, opt = 1, port;
    long v;
    char *end;
    struct sockaddr_in addr;


    /* =========================================================================
     * Step 1: Kiểm tra tham số dòng lệnh
     * ./server PortNumber, hợp lệ trong [1,65535].
     * ========================================================================= */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s PortNumber\n", argv[0]);
        return 1;
    }
    v = strtol(argv[1], &end, 10);
    if (*end != '\0' || v < 1 || v > 65535) {
        fprintf(stderr, "Cong khong hop le: %s\n", argv[1]);
        return 1;
    }
    port = (int)v;


    /* =========================================================================
     * Step 2: Bỏ qua SIGPIPE khi peer ngắt đột ngột
     * Gửi đến socket đã đóng sẽ trả lỗi, không giết cả server.
     * ========================================================================= */
    signal(SIGPIPE, SIG_IGN);


    /* =========================================================================
     * Step 3: Tạo socket TCP IPv4
     * AF_INET: IPv4; SOCK_STREAM: byte stream TCP; protocol 0: chọn TCP mặc định.
     * ========================================================================= */
    /*
     * socket(AF_INET, SOCK_STREAM, 0):
     *   Input: AF_INET = IPv4, SOCK_STREAM = TCP stream, 0 = mặc định.
     *   Output: descriptor >= 0; -1 khi hết descriptor/tài nguyên/quyền.
     */
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return 1;
    }

    /* =========================================================================
     * Step 4: Cấu hình cho phép tái sử dụng địa chỉ
     * SO_REUSEADDR hỗ trợ khởi động lại server sau khi socket cũ đóng.
     * ========================================================================= */
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);


    /* =========================================================================
     * Step 5: Thiết lập struct sockaddr_in và network byte order
     * htons: host-to-network short (16 bit port); htonl: host-to-network long (32 bit IP).
     * ========================================================================= */
    /*
     * sockaddr_in: địa chỉ IPv4 dùng cho bind/connect.
     * sin_family = AF_INET (IPv4); sin_addr = INADDR_ANY để nghe mọi NIC.
     * htons(port) chuyển 16 bit từ host byte order sang network byte order
     * (big-endian); htonl(INADDR_ANY) chuyển 32 bit địa chỉ tương ứng.
     */
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    /* =========================================================================
     * Step 6: Bind địa chỉ và số cổng
     * Gắn socket nghe với IP INADDR_ANY và port; lỗi thường do port đã sử dụng.
     * ========================================================================= */
    /*
     * bind(fd, addr, addrlen): gắn địa chỉ IP/port cho listening socket.
     *   Input: lfd = socket; &addr = địa chỉ; sizeof addr = số byte.
     *   Output: 0 thành công; -1 nếu cổng bị chiếm/không được phép.
     *   perror() in nguyên nhân system call dựa trên errno.
     */
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        return 1;
    }

    /* =========================================================================
     * Step 7: Chuyển socket sang trạng thái lắng nghe
     * Backlog 16 là số kết nối chờ accept (không phải tối đa 16 user).
     * ========================================================================= */
    /*
     * listen(fd, backlog): bắt đầu nhận yêu cầu bắt tay TCP.
     *   Input: lfd = bound socket; backlog = 16 kết nối đang chờ.
     *   Output: 0 thành công; -1 nếu descriptor/socket không hợp lệ.
     */
    if (listen(lfd, 16) < 0) {
        perror("listen");
        return 1;
    }
    printf("Server dang lang nghe tren cong %d\n", port);
    fflush(stdout);


    /* =========================================================================
     * Step 8: Vòng lặp accept kết nối mới
     * Mỗi lần accept thành công là một socket mới độc lập với lfd.
     * ========================================================================= */
    for (;;) {
        /*
         * accept(lfd, NULL, NULL): lấy một client khỏi hàng đợi TCP.
         *   Input: lfd = listening fd; NULL,NULL = không lấy peer address.
         *   Output: cfd >= 0 cho client mới; -1 nếu bị ngắt/lỗi mạng.
         *   accept có thể blocking, nhưng chỉ main bị chờ, worker vẫn chạy.
         */
        int cfd = accept(lfd, NULL, NULL);
        conn_t *c;
        pthread_t tid;
        pthread_attr_t attr;

        if (cfd < 0) {
            if (errno != EINTR)
                perror("accept");
            continue;
        }

    /* =========================================================================
     * Step 9: Tạo ClientContext cho một client
     * Cấp bộ nhớ zero-init; mỗi client có mutex ghi socket riêng.
     * ========================================================================= */
        c = calloc(1, sizeof *c);
        if (!c) {
            close(cfd);
            continue;
        }
        c->fd = cfd;
        pthread_mutex_init(&c->wlock, NULL);


    /* =========================================================================
     * Step 10: Tạo pthread xử lý client độc lập
     * Thread detached tự thu hồi tài nguyên sau khi kết thúc.
     * ========================================================================= */
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, conn_thread, c) != 0) {
            perror("pthread_create");
            pthread_mutex_destroy(&c->wlock);
            close(cfd);
            free(c);
        }
        pthread_attr_destroy(&attr);
    }
    return 0;
}
