#ifndef SERVER_H
#define SERVER_H

/*
 * Module: server.h
 * Chức năng: Khai báo kiểu dữ liệu, hằng số và prototype dùng chung cho server.
 * Được gọi bởi: server.c, server_account.c, server_session.c và
 *               server_protocol.c.
 * Không chứa implementation; implementation nằm trong các file .c tương ứng.
 */
#include <stddef.h>          /* Kiểu size_t dùng cho số lượng phần tử */
#include <sys/socket.h>      /* socklen_t dùng cho địa chỉ socket */
#include <netinet/in.h>      /* struct sockaddr_in dùng cho IPv4 */

#define ACCOUNT_FILE "account.txt" /* Tên file cơ sở dữ liệu tài khoản */
#define MAX_ACCOUNTS 128             /* Số lượng tài khoản tối đa */
#define MAX_SESSIONS 128             /* Số lượng phiên client tối đa */
#define MAX_TEXT 1024                /* Kích thước bộ đệm datagram */
#define USERNAME_SIZE 128            /* Kích thước bộ đệm username */
#define PASSWORD_SIZE 128            /* Kích thước bộ đệm password */

typedef struct {
    char username[USERNAME_SIZE]; /* Tên tài khoản */
    char password[PASSWORD_SIZE]; /* Mật khẩu trong account.txt */
    int status;                    /* 1 active, 0 blocked/not activated */
    unsigned int failed_attempts;  /* Số lần nhập sai liên tiếp */
} Account;

typedef enum {
    SESSION_WAIT_USERNAME, /* Phiên đang chờ username */
    SESSION_WAIT_PASSWORD, /* Phiên đang chờ password */
    SESSION_LOGGED_IN      /* Client đã đăng nhập */
} SessionState;

typedef struct {
    int active;                    /* 1 nếu slot đang được sử dụng */
    struct sockaddr_in address;    /* IP và ephemeral port của client */
    char username[USERNAME_SIZE]; /* Username trong phiên */
    Account *account;              /* Tài khoản tương ứng */
    SessionState state;            /* Trạng thái phiên */
} Session;

/**
 * @brief Kiểm tra và chuyển chuỗi port thành số nguyên hợp lệ.
 * @param[in] text Chuỗi port cần phân tích.
 * @param[out] port Nơi nhận giá trị port sau khi chuyển đổi.
 * @return 1 nếu hợp lệ, 0 nếu sai định dạng hoặc ngoài khoảng 1..65535.
 */
int parse_port(const char *text, int *port);

/**
 * @brief Đọc danh sách tài khoản từ account.txt.
 * @param[out] accounts Mảng nhận dữ liệu tài khoản.
 * @param[out] count Nơi nhận số lượng tài khoản đã đọc.
 * @return 1 nếu đọc thành công, 0 nếu không mở được file.
 */
int load_accounts(Account accounts[], size_t *count);

/**
 * @brief Ghi danh sách tài khoản vào account.txt.
 * @param[in] accounts Mảng tài khoản cần ghi.
 * @param[in] count Số lượng tài khoản hợp lệ trong mảng.
 * @return 1 nếu ghi thành công, 0 nếu thao tác file thất bại.
 */
int save_accounts(const Account accounts[], size_t count);

/**
 * @brief Tìm tài khoản theo username.
 * @param[in] accounts Mảng tài khoản cần tìm.
 * @param[in] count Số lượng phần tử hợp lệ.
 * @param[in] username Username cần tìm.
 * @return Con trỏ đến tài khoản tìm thấy hoặc NULL nếu không có.
 */
Account *find_account(Account accounts[], size_t count, const char *username);

/**
 * @brief Tìm hoặc cấp phiên cho client theo địa chỉ IP và port.
 * @param[in,out] sessions Bảng phiên được tìm và có thể được cập nhật.
 * @param[in] address Địa chỉ IP và port nguồn của client.
 * @return Con trỏ phiên tương ứng hoặc NULL nếu bảng phiên đã đầy.
 */
Session *get_session(Session sessions[], const struct sockaddr_in *address);

/**
 * @brief Xử lý datagram theo trạng thái phiên của client.
 * @param[in] socket_fd Socket UDP dùng để gửi phản hồi.
 * @param[in,out] session Phiên client đang được xử lý.
 * @param[in,out] accounts Danh sách tài khoản có thể được cập nhật.
 * @param[in] account_count Số lượng tài khoản hợp lệ.
 * @param[in] message Nội dung datagram nhận được.
 * @param[in] client Địa chỉ IP và port của client.
 * @param[in] client_length Kích thước địa chỉ client.
 * @return void. Hàm gửi phản hồi và cập nhật trạng thái phiên nếu cần.
 */
void process_datagram(int socket_fd, Session *session, Account accounts[],
                      size_t account_count, char *message,
                      struct sockaddr_in *client, socklen_t client_length);

#endif
