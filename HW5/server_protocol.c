/*
 * Module: server_protocol.c
 * Chức năng: Xử lý giao thức ứng dụng: username, password, khóa tài khoản,
 *            SHA-256, logout và gửi phản hồi cho client.
 * Được gọi bởi: server.c gọi process_datagram() cho mỗi datagram nhận được.
 * Gọi tới: server_account.c để tìm/lưu tài khoản; server_crypto.c để băm.
 */
#include <stdio.h>          /* snprintf và perror dùng cho thông báo */
#include <string.h>         /* strlen, strcmp xử lý chuỗi giao thức */
#include <ctype.h>          /* isalnum kiểm tra chuỗi hợp lệ */
#include <sys/socket.h>     /* sendto gửi phản hồi UDP */
#include "server.h"         /* Kiểu dữ liệu và hàm tài khoản */
#include "server_crypto.h"  /* Hàm SHA-256 thuần C */

/**
 * @brief Gửi phản hồi UDP đến đúng client bằng sendto().
 *
 * @param[in] socket_fd File descriptor của socket UDP server.
 * @param[in] text Chuỗi phản hồi, không bao gồm byte kết thúc '\0'.
 * @param[in] client Địa chỉ IPv4 và port đích.
 * @param[in] client_length Kích thước cấu trúc địa chỉ đích.
 * @return 1 nếu gửi thành công, 0 nếu sendto() trả về lỗi.
 */
static int send_text(int socket_fd, const char *text,
                     const struct sockaddr_in *client, socklen_t client_length)
{
    size_t length; /* Số byte của chuỗi cần gửi */
    ssize_t sent;  /* Số byte sendto() đã gửi */

    length = strlen(text);
    sent = sendto(socket_fd, text, length, 0,
                  (const struct sockaddr *)client, client_length);
    return sent >= 0;
}

/**
 * @brief Kiểm tra chuỗi có hợp lệ để mã hóa hay không.
 *
 * Chuỗi hợp lệ phải không rỗng và chỉ chứa ký tự chữ cái hoặc chữ số.
 *
 * @param[in] text Chuỗi cần kiểm tra.
 * @return 1 nếu hợp lệ, 0 nếu rỗng hoặc chứa ký tự đặc biệt.
 */
static int is_alphanumeric(const char *text)
{
    size_t index; /* Vị trí ký tự đang kiểm tra */

    if (text[0] == '\0') {
        return 0;
    }
    for (index = 0; text[index] != '\0'; index++) {
        if (!isalnum((unsigned char)text[index])) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Xử lý một datagram theo trạng thái phiên của client.
 *
 * Hàm xử lý username, password, chuỗi cần mã hóa và yêu cầu logout.
 *
 * @param[in] socket_fd Socket UDP dùng để gửi phản hồi.
 * @param[in,out] session Phiên client đang xử lý.
 * @param[in,out] accounts Danh sách tài khoản và số lần sai.
 * @param[in] account_count Số lượng tài khoản hợp lệ.
 * @param[in] message Nội dung datagram đã nhận.
 * @param[in] client Địa chỉ IP và port của client gửi.
 * @param[in] client_length Kích thước cấu trúc địa chỉ client.
 * @return void. Trạng thái session và dữ liệu tài khoản có thể được cập nhật.
 */
void process_datagram(int socket_fd, Session *session, Account accounts[],
                      size_t account_count, char *message,
                      struct sockaddr_in *client, socklen_t client_length)
{
    Account *account; /* Tài khoản đang xác thực */
    char hash[65];    /* SHA-256 gồm 64 ký tự hex và byte '\0' */
    char goodbye[USERNAME_SIZE + 10]; /* Thông điệp đăng xuất */

    if (session->state == SESSION_WAIT_USERNAME) {
        (void)snprintf(session->username, USERNAME_SIZE, "%s", message);
        session->account = find_account(accounts, account_count, message);
        session->state = SESSION_WAIT_PASSWORD;
        (void)send_text(socket_fd, "Insert password", client, client_length);
        return;
    }

    if (session->state == SESSION_WAIT_PASSWORD) {
        account = session->account;
        /* Tài khoản status = 0 luôn bị từ chối trước khi kiểm tra password. */
        if (account != NULL && account->status == 0) {
            (void)send_text(socket_fd, "Account not ready", client, client_length);
            session->state = SESSION_WAIT_USERNAME;
            session->account = NULL;
            return;
        }
        if (account != NULL && strcmp(account->password, message) == 0) {
            account->failed_attempts = 0;
            session->state = SESSION_LOGGED_IN;
            (void)send_text(socket_fd, "OK", client, client_length);
            return;
        }
        if (account != NULL) {
            account->failed_attempts++;
            if (account->failed_attempts >= 3) {
                account->status = 0;
                if (!save_accounts(accounts, account_count)) {
                    perror("Cannot update account file");
                }
                (void)send_text(socket_fd, "Account is blocked", client, client_length);
                session->state = SESSION_WAIT_USERNAME;
                session->account = NULL;
                return;
            }
        }
        (void)send_text(socket_fd, "Not OK", client, client_length);
        session->state = SESSION_WAIT_USERNAME;
        session->account = NULL;
        return;
    }

    if (message[0] == '\0' || strcmp(message, "bye") == 0) {
        (void)snprintf(goodbye, sizeof(goodbye), "Goodbye %s", session->username);
        (void)send_text(socket_fd, goodbye, client, client_length);
        session->active = 0;
        return;
    }
    if (!is_alphanumeric(message) || !make_sha256(message, hash)) {
        (void)send_text(socket_fd, "Error", client, client_length);
        return;
    }
    (void)send_text(socket_fd, hash, client, client_length);
}
