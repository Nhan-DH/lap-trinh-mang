/*
 * resolver.h - Header cho ung dung phan giai ten mien DNS
 *
 * Dinh nghia cau truc du lieu, hang so va khai bao ham cho module
 * phan giai DNS qua UDP socket. Chuong trinh tu xay dung goi DNS
 * theo RFC 1035 thay vi dung getaddrinfo().
 *
 * Giao thuc: DNS over UDP, port 53
 */

#ifndef RESOLVER_H
#define RESOLVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>

/* ========================== HANG SO ========================== */

#define DNS_PORT        53
#define BUF_SIZE        512     /* Gioi han goi DNS qua UDP (RFC 1035 S2.3.4) */
#define TIMEOUT_SEC     3
#define MAX_IPS         16

#define DNS_GOOGLE      "8.8.8.8"
#define DNS_CLOUDFLARE  "1.1.1.1"
#define DNS_CF_FAMILY   "1.1.1.3"   /* Chan malware + noi dung nguoi lon */

#define DNS_TYPE_A      1
#define DNS_CLASS_IN    1

/* ======================== CAU TRUC ========================== */

/*
 * DNS Header (RFC 1035 S4.1.1) - 12 bytes.
 * Tat ca truong multi-byte o network byte order tren wire.
 */
typedef struct {
    unsigned short id;          /* Transaction ID khop request/response */
    unsigned short flags;       /* QR, Opcode, AA, TC, RD, RA, Z, RCODE */
    unsigned short qdcount;     /* So luong question */
    unsigned short ancount;     /* So luong answer RR */
    unsigned short nscount;     /* So luong authority RR */
    unsigned short arcount;     /* So luong additional RR */
} dns_header_t;

/* Ket qua phan giai DNS */
typedef struct {
    char official_ip[INET_ADDRSTRLEN];          /* IP chinh thuc (dau tien) */
    char alias_ips[MAX_IPS][INET_ADDRSTRLEN];   /* Danh sach IP phu */
    int alias_count;
    int found;                                  /* 1 neu co it nhat 1 ban ghi A */
} dns_result_t;

/* ===================== KHAI BAO HAM ========================= */

/**
 * @brief Tao goi DNS query hoan chinh cho ban ghi A (IPv4).
 *
 * Xay dung goi tin DNS theo RFC 1035 voi cau truc:
 * [Header 12B][QNAME encoded][QTYPE 2B][QCLASS 2B].
 * Flag RD=1 yeu cau server thuc hien recursive resolution,
 * nho do client khong can tu di hoi tung authoritative server.
 *
 * @param[in]  domain   Ten mien can phan giai (vd: "google.com").
 * @param[out] buf      Buffer chua goi tin ket qua (toi thieu BUF_SIZE bytes).
 *                      Buffer se bi memset(0) truoc khi ghi.
 * @param[in]  query_id Transaction ID de khop request voi response.
 *                      Nen dung gia tri unique (vd: PID) de tranh nham.
 *
 * @return Tong kich thuoc goi tin da tao (bytes), luon > sizeof(dns_header_t).
 */
int build_dns_query(const char *domain, unsigned char *buf,
                    unsigned short query_id);

/**
 * @brief Trich xuat cac ban ghi A (IPv4) tu DNS response.
 *
 * Duyet qua phan Question (bo qua), roi phan tich tung Answer RR.
 * Moi RR co cau truc: [NAME][TYPE 2B][CLASS 2B][TTL 4B][RDLENGTH 2B][RDATA].
 * Chi xu ly TYPE=A (RDATA = 4 bytes IPv4), bo qua CNAME va type khac.
 *
 * IP dau tien tim duoc duoc luu vao official_ip, cac IP sau vao alias_ips.
 * Ham xu ly ca label thuong va compression pointer trong truong Name.
 *
 * @param[in]  buf    Buffer chua DNS response nhan tu server.
 * @param[in]  len    Kich thuoc thuc te cua response (bytes).
 * @param[out] result Con tro toi struct ket qua. Cac truong official_ip,
 *                    alias_ips, alias_count, found se duoc cap nhat.
 *                    Nen memset(0) truoc khi goi.
 *
 * @note Ham khong tra ve gia tri; kiem tra result->found de biet co
 *       ban ghi A nao duoc tim thay khong.
 * @note Neu RCODE != 0 (server bao loi), ham return ngay ma khong
 *       thay doi result.
 */
void parse_dns_response(const unsigned char *buf, int len,
                        dns_result_t *result);

/**
 * @brief Gui DNS query va nhan response qua UDP socket.
 *
 * Thuc hien toan bo quy trinh xu ly socket cho 1 lan query:
 *   socket(UDP) -> setsockopt(timeout) -> connect() -> send() -> recv() -> close()
 *
 * Dung connect() tren UDP socket thay vi sendto()/recvfrom() vi:
 *   - Gan default destination, don gian hoa send()/recv()
 *   - Kernel tu loc bo packet tu nguon khac (tang security)
 *   - Nhan loi ICMP port unreachable qua recv() thay vi am tham bo qua
 *
 * Dat SO_RCVTIMEO de recv() khong block vo han khi server khong phan hoi
 * (mat mang, server down, packet bi drop boi firewall).
 *
 * @param[in]  server_ip     Dia chi IP cua DNS server (chuoi, vd: "8.8.8.8").
 * @param[in]  query         Goi DNS query da xay dung boi build_dns_query().
 * @param[in]  query_len     Kich thuoc goi query (bytes).
 * @param[out] response      Buffer nhan DNS response tu server.
 * @param[in]  response_size Kich thuoc toi da cua buffer response.
 *
 * @return So bytes nhan duoc (> 0 neu thanh cong).
 * @return -1 neu loi: socket() that bai, connect() that bai,
 *         send() khong gui du, hoac recv() timeout/loi.
 *
 * @warning Ham block toi da TIMEOUT_SEC giay neu server khong phan hoi.
 *          Socket luon duoc close() truoc khi return (ke ca khi loi).
 */
int send_dns_query(const char *server_ip, const unsigned char *query,
                   int query_len, unsigned char *response,
                   int response_size);

/**
 * @brief Kiem tra domain co bi chan boi Cloudflare Family Filter.
 *
 * Gui DNS query toi Cloudflare 1.1.1.3 (resolver loc noi dung nguoi lon
 * va malware). Khi domain bi chan, server tra ve IP 0.0.0.0 thay vi
 * IP that, hoac khong tra ban ghi A nao.
 *
 * Ham nay chi nen goi SAU KHI da xac nhan domain ton tai qua DNS thuong
 * (8.8.8.8 hoac 1.1.1.1), de phan biet:
 *   - "domain khong ton tai" (NXDOMAIN that)
 *   - "domain bi chan vi noi dung" (NXDOMAIN/0.0.0.0 chi tu family filter)
 *
 * @param[in] domain Ten mien can kiem tra (vd: "example.com").
 *
 * @return 1 neu domain bi chan (0.0.0.0 hoac khong co ban ghi A).
 * @return 0 neu domain khong bi chan, hoac khong ket noi duoc toi
 *         server 1.1.1.3 (mac dinh cho qua khi khong du du lieu).
 */
int is_domain_blocked(const char *domain);

#endif /* RESOLVER_H */
