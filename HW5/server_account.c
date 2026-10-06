/*
 * Module: server_account.c
 * Chức năng: Phân tích port và quản lý cơ sở dữ liệu account.txt.
 * Được gọi bởi: server.c gọi parse_port(), load_accounts();
 *               server_protocol.c gọi find_account(), save_accounts().
 * Gọi tới: thư viện stdio để đọc/ghi file và thư viện chuẩn để xử lý chuỗi.
 */
#include <stdio.h>          /* FILE, fopen, fscanf, fprintf, fclose */
#include <stdlib.h>         /* strtol và kiểu long */
#include <string.h>         /* strcmp và snprintf */
#include <errno.h>          /* errno dùng để kiểm tra strtol */
#include "server.h"         /* Kiểu Account và hằng số dùng chung */

/**
 * @brief Chuyển chuỗi port thành số và kiểm tra phạm vi hợp lệ. 
 *
 * @param[in] text Chuỗi port nhận từ dòng lệnh.
 * @param[out] port Nơi lưu port sau khi chuyển đổi thành công.
 * @return 1 nếu port hợp lệ, 0 nếu chuỗi sai hoặc ngoài khoảng 1..65535.
 */
int parse_port(const char *text, int *port)
{
    char *end_pointer; /* Vị trí kết thúc phần số */
    long value;        /* Giá trị port tạm thời */

    errno = 0;
    value = strtol(text, &end_pointer, 10);
    if (errno != 0 || text[0] == '\0' || *end_pointer != '\0' ||
        value < 1 || value > 65535) {
        return 0;
    }
    *port = (int)value;
    return 1;
}

/**
 * @brief Đọc danh sách tài khoản từ account.txt.
 *
 * @param[out] accounts Mảng nhận username, password và status.
 * @param[out] count Nơi lưu số lượng tài khoản đã đọc.
 * @return 1 nếu mở và đọc file thành công, 0 nếu không mở được file.
 */
int load_accounts(Account accounts[], size_t *count)
{
    FILE *file;                 /* File account.txt đang đọc */
    char username[USERNAME_SIZE]; /* Username tạm thời */
    char password[PASSWORD_SIZE]; /* Password tạm thời */
    int status;                 /* Status tạm thời */

    file = fopen(ACCOUNT_FILE, "r");
    if (file == NULL) {
        return 0;
    }
    *count = 0;
    while (*count < MAX_ACCOUNTS &&
           fscanf(file, "%127s %127s %d", username, password, &status) == 3) {
        (void)snprintf(accounts[*count].username, USERNAME_SIZE, "%s", username);
        (void)snprintf(accounts[*count].password, PASSWORD_SIZE, "%s", password);
        accounts[*count].status = status ? 1 : 0;
        accounts[*count].failed_attempts = 0;
        (*count)++;
    }
    (void)fclose(file);
    return 1;
}

/**
 * @brief Ghi toàn bộ danh sách tài khoản trở lại account.txt.
 *
 * @param[in] accounts Mảng tài khoản cần ghi.
 * @param[in] count Số lượng phần tử hợp lệ trong mảng.
 * @return 1 nếu ghi thành công, 0 nếu không mở hoặc không ghi được file.
 */
int save_accounts(const Account accounts[], size_t count)
{
    FILE *file;   /* File account.txt đang ghi đè */
    size_t index; /* Chỉ số tài khoản đang ghi */

    file = fopen(ACCOUNT_FILE, "w");
    if (file == NULL) {
        return 0;
    }
    for (index = 0; index < count; index++) {
        if (fprintf(file, "%s %s %d\n", accounts[index].username,
                    accounts[index].password, accounts[index].status) < 0) {
            (void)fclose(file);
            return 0;
        }
    }
    return fclose(file) == 0;
}

/**
 * @brief Tìm tài khoản theo username.
 *
 * @param[in] accounts Mảng tài khoản cần tìm.
 * @param[in] count Số lượng tài khoản trong mảng.
 * @param[in] username Username cần tìm.
 * @return Con trỏ đến tài khoản tìm thấy hoặc NULL nếu không tồn tại.
 */
Account *find_account(Account accounts[], size_t count, const char *username)
{
    size_t index; /* Chỉ số tài khoản đang so sánh */

    for (index = 0; index < count; index++) {
        if (strcmp(accounts[index].username, username) == 0) {
            return &accounts[index];
        }
    }
    return NULL;
}
