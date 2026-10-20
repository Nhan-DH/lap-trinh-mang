/* net.c - lop nhap/xuat tren TCP stream. SINH VIEN CAI DAT cac ham duoi day.
 * Xem mo ta khung tin trong de bai (muc 3) va khai bao trong proto.h. */
#define _GNU_SOURCE
#include "proto.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Gui du n byte. Xu ly gui thieu (send tra ve < n) va EINTR.
 * Dung MSG_NOSIGNAL de khong bi SIGPIPE.
 * Tra ve n neu thanh cong, -1 neu loi. */
ssize_t send_all(int fd, const void *buf, size_t n)
{
    const uint8_t *data = buf;
    size_t total = 0;

    while (total < n) {
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

/* Nhan du n byte. Xu ly nhan thieu va EINTR.
 * Tra ve: n neu du; 0 neu doi phuong dong ket noi truoc khi gui byte nao;
 *         -1 neu loi hoac doi phuong dong giua chung. */
ssize_t recv_all(int fd, void *buf, size_t n)
{
    uint8_t *data = buf;
    size_t total = 0;

    while (total < n) {
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

/* Dung khung vao out: header 16 byte (big-endian) + payload.
 * CRC32 tinh tren 12 byte dau cua header noi voi payload (xem de bai),
 * ghi vao byte 12..15. Tra ve tong so byte cua khung.
 * Luu y: payload co the trung vung nho voi out + HDR_LEN (dung memmove). */
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

/* Dung khung roi gui bang send_all. Tra ve 0 neu thanh cong, -1 neu loi. */
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

/* Nhan tron mot khung: doc 16 byte header, doc len byte payload (vao payload,
 * bo dem toi thieu MAX_PAYLOAD + 1 byte), kiem tra CRC32.
 * Ma tra ve: RF_OK, RF_EOF, RF_ERR, RF_CRC, RF_PROTO (xem proto.h).
 * Voi RF_CRC phai da doc het payload de luong byte khong bi lech khung. */
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
