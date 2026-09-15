/*
 * main.c - Entry point cho ung dung phan giai ten mien DNS
 *
 * Nhan ten mien tu tham so dong lenh, phan giai sang dia chi IP,
 * kiem tra domain doc hai, va xu ly loi ket noi mang.
 *
 * Su dung: ./resolver <domain>
 */

#include "resolver.h"


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <domain>\n", argv[0]);
        return 1;
    }

    const char *domain = argv[1];
    dns_result_t result;
    memset(&result, 0, sizeof(result));

    unsigned char query[BUF_SIZE], response[BUF_SIZE];

    /*
     * Dung PID lam transaction ID: du unique trong cung thoi diem,
     * tranh khop nham voi response tu query cu bi tre tren mang.
     */
    int qlen = build_dns_query(domain, query,
                               (unsigned short)(getpid() & 0xFFFF));

    /*
     * Thu phan giai lan luot qua 2 DNS server (8.8.8.8, 1.1.1.1).
     * dns_reachable phan biet 2 truong hop that bai khac nhau:
     *   - Server phan hoi nhung domain khong ton tai -> "Not found information"
     *   - Khong server nao phan hoi -> "No internet connection"
     */
    int dns_reachable = 0;
    const char *dns_servers[] = { DNS_GOOGLE, DNS_CLOUDFLARE };

    for (int i = 0; i < 2 && !result.found; i++) {
        int rlen = send_dns_query(dns_servers[i], query, qlen,
                                  response, BUF_SIZE);
        if (rlen > 0) {
            dns_reachable = 1;
            parse_dns_response(response, rlen, &result);
        }
    }

    /* Ca 2 server deu khong phan hoi -> mat ket noi Internet */
    if (!dns_reachable) {
        printf("No internet connection\n");
        return 1;
    }

    /* Server phan hoi nhung domain khong co ban ghi A */
    if (!result.found) {
        printf("Not found information\n");
        return 0;
    }

    /* Hien thi ket qua phan giai Official IP & Alias IP */
    printf("Official IP: %s\n", result.official_ip);
    if (result.alias_count > 0) {
        printf("Alias IP:\n");
        for (int i = 0; i < result.alias_count; i++) {
            printf("%s\n", result.alias_ips[i]);
        }
    }

    /* Kiem tra noi dung nguoi lon qua Cloudflare Family Filter (1.1.1.3) */
    if (is_domain_blocked(domain)) {
        printf("This site is not for you!\n");
    }

    return 0;
}
