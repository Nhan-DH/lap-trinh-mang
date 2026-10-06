/*
 * Module: server_session.c
 * Chức năng: Tìm hoặc cấp phiên độc lập cho từng client UDP theo IP và port.
 * Được gọi bởi: server.c gọi get_session() sau mỗi recvfrom().
 * Gọi tới: server.h để dùng Session, MAX_SESSIONS và struct sockaddr_in.
 */
#include <stddef.h>         /* Kiểu size_t */
#include "server.h"         /* Session, MAX_SESSIONS và sockaddr_in */

/**
 * @brief So sánh hai địa chỉ client UDP.
 *
 * So sánh cả địa chỉ IP và số hiệu port nguồn.
 *
 * @param[in] left Địa chỉ client thứ nhất.
 * @param[in] right Địa chỉ client thứ hai.
 * @return 1 nếu cùng IP và port, 0 nếu khác nhau.
 */
static int same_client(const struct sockaddr_in *left,
                       const struct sockaddr_in *right)
{
    return left->sin_addr.s_addr == right->sin_addr.s_addr &&
           left->sin_port == right->sin_port;
}

/**
 * @brief Tìm phiên đã cấp cho client theo IP và ephemeral port.
 *
 * @param[in] sessions Bảng các phiên client.
 * @param[in] address Địa chỉ nguồn của datagram vừa nhận.
 * @return Con trỏ đến phiên tìm thấy hoặc NULL nếu client chưa có phiên.
 */
static Session *find_session(Session sessions[],
                             const struct sockaddr_in *address)
{
    size_t index; /* Chỉ số slot phiên đang kiểm tra */

    for (index = 0; index < MAX_SESSIONS; index++) {
        if (sessions[index].active &&
            same_client(&sessions[index].address, address)) {
            return &sessions[index];
        }
    }
    return NULL;
}

/**
 * @brief Tìm phiên cũ hoặc cấp slot mới cho client.
 *
 * @param[in,out] sessions Bảng phiên cần tìm hoặc cập nhật.
 * @param[in] address Địa chỉ IP và port nguồn của client.
 * @return Con trỏ đến phiên tương ứng hoặc NULL nếu bảng phiên đã đầy.
 */
Session *get_session(Session sessions[], const struct sockaddr_in *address)
{
    size_t index; /* Chỉ số slot đang tìm */
    Session *session; /* Phiên hiện tại hoặc phiên mới */

    session = find_session(sessions, address);
    if (session != NULL) {
        return session;
    }
    for (index = 0; index < MAX_SESSIONS; index++) {
        if (!sessions[index].active) {
            sessions[index].active = 1;
            sessions[index].address = *address;
            sessions[index].username[0] = '\0';
            sessions[index].account = NULL;
            sessions[index].state = SESSION_WAIT_USERNAME;
            return &sessions[index];
        }
    }
    return NULL;
}
