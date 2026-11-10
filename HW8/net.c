/*
 * net.c - Gửi/nhận frame và tách dòng lệnh (ĐÃ CUNG CẤP, không cần sửa)
 *
 * Định dạng frame trên TCP:
 *     [LEN: 4 byte big-endian][IV: 16 byte][CIPHERTEXT: LEN-16 byte]
 * Plaintext (trước khi mã hóa) = HEADER + '\n' + BODY
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "common.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   /* macOS: dùng signal(SIGPIPE, SIG_IGN) thay thế */
#endif

int send_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;

    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

int recv_all(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;

    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n == 0)                 /* đối phương đã đóng kết nối */
            return -1;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

int send_msg(int fd, const char *header, const void *body, size_t blen)
{
    size_t hlen = strlen(header);
    size_t plen = hlen + 1 + blen;
    unsigned char *plain, *enc, *frame;
    size_t clen;
    uint32_t n_net;
    int rc = -1;

    plain = malloc(plen);
    if (!plain)
        return -1;
    memcpy(plain, header, hlen);
    plain[hlen] = '\n';
    if (blen > 0)
        memcpy(plain + hlen + 1, body, blen);

    enc = crypto_encrypt(plain, plen, &clen);
    free(plain);
    if (!enc)
        return -1;
    if (clen > MAX_FRAME_LEN) {
        free(enc);
        return -1;
    }

    /* Ghép LEN + IV||CIPHERTEXT vào một buffer để gửi bằng đúng một lần send_all */
    frame = malloc(4 + clen);
    if (!frame) {
        free(enc);
        return -1;
    }
    n_net = htonl((uint32_t)clen);
    memcpy(frame, &n_net, 4);
    memcpy(frame + 4, enc, clen);
    if (send_all(fd, frame, 4 + clen) == 0)
        rc = 0;

    free(frame);
    free(enc);
    return rc;
}

int recv_msg(int fd, unsigned char **plain, size_t *plen)
{
    unsigned char lenbuf[4];
    uint32_t n_net, n;
    unsigned char *buf, *p;
    size_t outlen = 0;

    if (recv_all(fd, lenbuf, 4) != 0)
        return RECV_IO;
    memcpy(&n_net, lenbuf, 4);
    n = ntohl(n_net);

    /* Kiểm tra LEN trước khi cấp phát/đọc để chống frame khổng lồ */
    if (n > MAX_FRAME_LEN)
        return RECV_TOO_BIG;

    buf = malloc(n > 0 ? n : 1);
    if (!buf)
        return RECV_IO;
    if (recv_all(fd, buf, n) != 0) {
        free(buf);
        return RECV_IO;
    }

    p = crypto_decrypt(buf, n, &outlen);
    free(buf);
    if (!p)
        return RECV_BAD;

    *plain = p;
    *plen = outlen;
    return RECV_OK;
}

int split_msg(const unsigned char *plain, size_t plen,
              char *header, size_t hcap,
              const unsigned char **body, size_t *blen)
{
    const unsigned char *nl = memchr(plain, '\n', plen);
    size_t hl;

    if (!nl)
        return -1;
    hl = (size_t)(nl - plain);
    if (hl >= hcap)
        return -1;

    memcpy(header, plain, hl);
    header[hl] = '\0';
    *body = nl + 1;
    *blen = plen - hl - 1;
    return 0;
}
