/*
 * resolver.c - Implementation cac ham phan giai DNS
 *
 * Module nay xay dung va phan tich goi tin DNS theo RFC 1035,
 * gui/nhan qua UDP socket. Cung cap kha nang kiem tra domain
 * doc hai thong qua Cloudflare Family Filter (1.1.1.3).
 *
 * Cac ham static (encode_dns_name, skip_dns_name) la helper noi bo,
 * chi duoc goi boi cac ham public khai bao trong resolver.h.
 */

#include "resolver.h"


/* ==================== HAM HELPER NOI BO ===================== */

/*
 * encode_dns_name - Chuyen ten mien sang DNS wire format (label-length encoding).
 *
 * DNS khong dung dau cham truc tiep. Moi phan giua cac dau cham
 * duoc ma hoa thanh: [byte do dai][du lieu], ket thuc bang byte 0x00.
 * Vi du: "google.com" -> \x06google\x03com\x00
 *
 * Tra ve: so bytes da ghi vao buf.
 */
static int encode_dns_name(const char *domain, unsigned char *buf) {
    int pos = 0;
    const char *start = domain;
    const char *dot;

    while ((dot = strchr(start, '.')) != NULL) {
        int label_len = dot - start;
        buf[pos++] = (unsigned char)label_len;
        memcpy(buf + pos, start, label_len);
        pos += label_len;
        start = dot + 1;
    }

    /* Phan cuoi sau dau cham cuoi cung (hoac toan bo neu khong co cham) */
    int last_len = strlen(start);
    if (last_len > 0) {
        buf[pos++] = (unsigned char)last_len;
        memcpy(buf + pos, start, last_len);
        pos += last_len;
    }

    buf[pos++] = 0; /* Null terminator danh dau ket thuc QNAME */
    return pos;
}


/*
 * skip_dns_name - Bo qua truong Name trong DNS response.
 *
 * DNS dung 2 dang encoding cho Name:
 *   - Label thuong: [length 1B][data NB]... ket thuc bang 0x00
 *   - Compression pointer: 2 bytes bat dau bang 0xC0, tro toi vi tri
 *     khac trong goi tin de tranh lap lai ten mien (RFC 1035 S4.1.4)
 *
 * Tra ve: so bytes ma truong Name chiem tai vi tri offset.
 *         Pointer luon chiem dung 2 bytes bat ke ten that dai bao nhieu.
 */
static int skip_dns_name(const unsigned char *buf, int offset, int buf_len) {
    int count = 0;
    int jumped = 0;

    while (offset < buf_len) {
        unsigned char len = buf[offset];

        if (len == 0) {
            if (!jumped) count++;
            break;
        }

        /* 2 bit dau = 11 -> compression pointer (14 bit con lai = offset) */
        if ((len & 0xC0) == 0xC0) {
            if (!jumped) count += 2;
            break;
        }

        if (!jumped) count += 1 + len;
        offset += 1 + len;
    }

    return count;
}


/* ==================== HAM PUBLIC ============================ */

int build_dns_query(const char *domain, unsigned char *buf,
                    unsigned short query_id) {
    memset(buf, 0, BUF_SIZE);

    dns_header_t *header = (dns_header_t *)buf;
    header->id = htons(query_id);
    header->flags = htons(0x0100);  /* RD=1: recursive query */
    header->qdcount = htons(1);     /* 1 question */

    /* Encode ten mien ngay sau header */
    int name_len = encode_dns_name(domain, buf + sizeof(dns_header_t));

    /* QTYPE va QCLASS dat ngay sau QNAME, moi truong 2 bytes big-endian */
    int qfield_offset = sizeof(dns_header_t) + name_len;
    *(unsigned short *)(buf + qfield_offset)     = htons(DNS_TYPE_A);
    *(unsigned short *)(buf + qfield_offset + 2) = htons(DNS_CLASS_IN);

    return qfield_offset + 4;
}


void parse_dns_response(const unsigned char *buf, int len,
                        dns_result_t *result) {
    if (len < (int)sizeof(dns_header_t)) return;

    dns_header_t *header = (dns_header_t *)buf;
    int ancount = ntohs(header->ancount);
    int qdcount = ntohs(header->qdcount);

    /* RCODE nam o 4 bit cuoi cua flags. 0 = No Error, khac 0 = server loi */
    int rcode = ntohs(header->flags) & 0x000F;
    if (rcode != 0) return;

    int offset = sizeof(dns_header_t);

    /* Bo qua phan Question: moi question gom QNAME + QTYPE(2) + QCLASS(2) */
    for (int i = 0; i < qdcount; i++) {
        offset += skip_dns_name(buf, offset, len);
        offset += 4;
    }

    /* Duyet phan Answer, trich xuat ban ghi A */
    for (int i = 0; i < ancount && offset < len; i++) {
        offset += skip_dns_name(buf, offset, len);

        /* Can it nhat 10 bytes cho phan co dinh cua RR (TYPE+CLASS+TTL+RDLENGTH) */
        if (offset + 10 > len) break;

        unsigned short type = ntohs(*(unsigned short *)(buf + offset));
        offset += 2;   /* TYPE */
        offset += 2;   /* CLASS */
        offset += 4;   /* TTL */
        unsigned short rdlength = ntohs(*(unsigned short *)(buf + offset));
        offset += 2;   /* RDLENGTH */

        if (type == DNS_TYPE_A && rdlength == 4) {
            /* RDATA = 4 bytes IPv4 address o network byte order */
            struct in_addr addr;
            memcpy(&addr, buf + offset, 4);

            if (!result->found) {
                inet_ntop(AF_INET, &addr, result->official_ip,
                          INET_ADDRSTRLEN);
                result->found = 1;
            } else if (result->alias_count < MAX_IPS) {
                inet_ntop(AF_INET, &addr,
                          result->alias_ips[result->alias_count],
                          INET_ADDRSTRLEN);
                result->alias_count++;
            }
        }

        offset += rdlength;
    }
}


int send_dns_query(const char *server_ip, const unsigned char *query,
                   int query_len, unsigned char *response,
                   int response_size) {
    /* DNS uses UDP for ordinary queries, so one datagram fits one exchange. */
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) return -1;

    /* The timeout prevents recv() from blocking forever when UDP is dropped. */
    struct timeval tv = { .tv_sec = TIMEOUT_SEC, .tv_usec = 0 };
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &tv, sizeof(tv)) < 0) {
        close(sockfd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DNS_PORT);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
        close(sockfd);
        return -1;
    }

    /* connect() fixes the peer and lets recv() reject unrelated packets. */
    if (connect(sockfd, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    /* UDP send is datagram-oriented; a short send means the query was not sent. */
    if (send(sockfd, query, query_len, 0) != query_len) {
        close(sockfd);
        return -1;
    }

    /* The receive length is needed because DNS responses are binary packets. */
    int recv_len = recv(sockfd, response, response_size, 0);

    /* close() also runs after timeout/error so every socket has one owner. */
    close(sockfd);

    return recv_len;
}


int is_domain_blocked(const char *domain) {
    unsigned char query[BUF_SIZE], response[BUF_SIZE];

    int qlen = build_dns_query(domain, query, 0xCCCC);

    /* Thuc hien query toi 1.1.1.3 (thu toi da 2 lan phong tranh UDP packet loss) */
    int rlen = -1;
    for (int retry = 0; retry < 2 && rlen <= 0; retry++) {
        rlen = send_dns_query(DNS_CF_FAMILY, query, qlen, response, BUF_SIZE);
    }

    /* Server khong phan hoi -> khong du du lieu ket luan, mac dinh cho qua */
    if (rlen <= 0) return 0;

    dns_result_t cf_result;
    memset(&cf_result, 0, sizeof(cf_result));
    parse_dns_response(response, rlen, &cf_result);

    /* IP 0.0.0.0 la tin hieu ro rang Cloudflare Family Filter da chan domain nay */
    if (cf_result.found) {
        if (strcmp(cf_result.official_ip, "0.0.0.0") == 0) return 1;
        for (int i = 0; i < cf_result.alias_count; i++) {
            if (strcmp(cf_result.alias_ips[i], "0.0.0.0") == 0) return 1;
        }
    } else {
        /* Domain ton tai tren DNS thuong nhung bi filter chan o 1.1.1.3 (NXDOMAIN) */
        return 1;
    }

    return 0;
}
