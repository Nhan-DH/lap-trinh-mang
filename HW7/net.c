/*
 * net.c - Tang dong goi frame tren TCP stream cua HW7.
 *
 * TCP khong bao toan ranh gioi message, vi vay file nay cung cap cac ham
 * gui/nhan du byte, tao frame nhi phan big-endian va kiem tra CRC32.
 */
#define _GNU_SOURCE
#include "proto.h"          /* Hi?u ??c frame v? khai b?o h?m d?ng chung */
#include <errno.h>          /* M? l?i errno v? EINTR */
#include <string.h>         /* memmove d?ng khi ??ng g?i d? li?u t?i */
#include <sys/socket.h>     /* g?i, nh?n v? c?c hang s? socket */
#include <unistd.h>         /* C?c h?m h? th?ng POSIX */

/**
 * @brief Gui du n byte qua TCP.
 * @param fd Socket TCP.
 * @param buf Vung d? li?u can gui.
 * @param n So byte can gui.
 * @return n neu th?nh cung, -1 neu gap loi.
 */
ssize_t send_all(int fd, const void *buf, size_t n)
{
    const uint8_t *data = buf;
    size_t total = 0;

    while (total < n) {
        /*
         * Hàm send(): gửi một phần hoặc toàn bộ dữ liệu qua socket TCP.
         *   - fd: file descriptor của socket đã kết nối.
         *   - data + total: vùng dữ liệu chưa gửi.
         *   - n - total: số byte còn lại cần gửi.
         *   - MSG_NOSIGNAL: không phát sinh SIGPIPE khi peer đã đóng.
         *   - Giá trị trả về: số byte đã gửi, hoặc -1 nếu lỗi.
         */
        ssize_t sent = send(fd, data + total, n - total, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        total += (size_t)sent;
    }
    return (ssize_t)total;
}

/**
 * @brief Nh?n du n byte qua TCP.
 * @param[in] fd Socket TCP.
 * @param[out] buf Bo dem nhan d? li?u.
 * @param[in] n So byte can nhan.
 * @return n neu nhan du, 0 neu peer dong ngay tu dau, -1 neu dong gi?a chung
 *         hoac gap loi I/O.
 */
ssize_t recv_all(int fd, void *buf, size_t n)
{
    uint8_t *data = buf;
    size_t total = 0;

    while (total < n) {
        /*
         * Hàm recv(): nhận dữ liệu từ socket TCP.
         *   - fd: file descriptor của socket đã kết nối.
         *   - data + total: vùng nhớ ghi dữ liệu nhận được.
         *   - n - total: số byte tối đa cần nhận.
         *   - 0: không dùng cờ đặc biệt.
         *   - Giá trị trả về: số byte nhận được, 0 khi peer đóng,
         *     hoặc -1 nếu xảy ra lỗi.
         */
        ssize_t received = recv(fd, data + total, n - total, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (received == 0) {
            return total == 0 ? 0 : -1;
        }
        total += (size_t)received;
    }
    return (ssize_t)total;
}

/**
 * @brief Tao mot frame gom header 16 byte va payload.
 * @param[out] out Bo dem nhan frame, phai du HDR_LEN + len byte.
 * @param[in] type Loai frame trong enum frame_type.
 * @param[in] wnd Cua so nhan server quang ba cho client.
 * @param[in] seq So thu tu cua frame hoac chunk.
 * @param[in] payload Du lieu ung d?ng can dong goi, co the NULL neu len = 0.
 * @param[in] len So byte payload.
 * @return Tong kich thuoc frame; 0 neu tham so kh?ng h?p l?.
 *
 * CRC duoc tinh tren 12 byte dau header noi v?i payload.
 */
size_t build_frame(uint8_t *out, uint8_t type, uint16_t wnd, uint32_t seq,
                   const void *payload, uint32_t len)
{
    uint32_t crc;

    if (out == NULL || len > MAX_PAYLOAD || (len > 0 && payload == NULL)) {
        return 0;
    }

    out[0] = type;
    out[1] = 0;
    put16(out + 2, wnd);
    put32(out + 4, seq);
    put32(out + 8, len);
    memmove(out + HDR_LEN, payload, len);

    crc = crc32_calc(out, 12, 0);
    crc = crc32_calc(out + HDR_LEN, len, crc);
    put32(out + 12, crc);
    return HDR_LEN + (size_t)len;
}

/**
 * @brief Tao va gui tron mot frame qua socket.
 * @param[in] fd Socket TCP da ket noi.
 * @param[in] type Loai frame trong enum frame_type.
 * @param[in] wnd Cua so nhan can quang ba.
 * @param[in] seq So thu tu frame.
 * @param[in] payload Payload can gui.
 * @param[in] len So byte payload.
 * @return 0 neu th?nh cung, -1 neu frame kh?ng h?p l? hoac gui loi.
 */
int send_frame(int fd, uint8_t type, uint16_t wnd, uint32_t seq,
               const void *payload, uint32_t len)
{
    uint8_t frame[HDR_LEN + MAX_PAYLOAD];
    size_t frame_len = build_frame(frame, type, wnd, seq, payload, len);

    if (frame_len == 0) {
        errno = EINVAL;
        return -1;
    }
    return send_all(fd, frame, frame_len) < 0 ? -1 : 0;
}

/**
 * @brief Nh?n, giai ma va kiem tra CRC cua mot frame.
 * @param[in] fd Socket TCP da ket noi.
 * @param[out] h Header da giai ma.
 * @param[out] payload Bo dem nhan payload.
 * Payload van duoc doc het truoc khi tra RF_CRC de gi? TCP stream ??ng b?.
 * @return RF_OK, RF_EOF, RF_ERR, RF_CRC hoac RF_PROTO.
 */
int recv_frame(int fd, frame_hdr_t *h, uint8_t *payload)
{
    uint8_t header[HDR_LEN];
    uint32_t received_crc;
    uint32_t calculated_crc;
    ssize_t result;

    if (h == NULL) {
        errno = EINVAL;
        return RF_ERR;
    }

    result = recv_all(fd, header, sizeof header);
    if (result == 0) {
        return RF_EOF;
    }
    if (result < 0) {
        return RF_ERR;
    }

    h->type = header[0];
    h->flags = header[1];
    h->wnd = get16(header + 2);
    h->seq = get32(header + 4);
    h->len = get32(header + 8);
    received_crc = get32(header + 12);

    if (h->len > MAX_PAYLOAD) {
        return RF_PROTO;
    }
    if (h->len > 0 && payload == NULL) {
        errno = EINVAL;
        return RF_ERR;
    }
    if (recv_all(fd, payload, h->len) < 0) {
        return RF_ERR;
    }

    calculated_crc = crc32_calc(header, 12, 0);
    calculated_crc = crc32_calc(payload, h->len, calculated_crc);
    if (calculated_crc != received_crc) {
        return RF_CRC;
    }
    return RF_OK;
}
