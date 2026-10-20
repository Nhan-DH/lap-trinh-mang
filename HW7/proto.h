#ifndef PROTO_H
#define PROTO_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* ---- Tham so giao thuc ---- */
#define CHUNK_SIZE    4096              /* so byte du lieu toi da trong mot DATA */
#define HDR_LEN       16                /* do dai header khung tin */
#define MAX_PAYLOAD   (CHUNK_SIZE + 512)
#define MAX_STR       1024              /* do dai toi da xau o Che do 1 */
#define MAX_NAME      255               /* do dai toi da ten file */
#define DEFAULT_BUF   16                /* so o nho dem mac dinh cua server */
#define MAX_INFLIGHT  256               /* so chunk toi da dang bay cua client */
#define RTO_INIT_MS   200
#define RTO_MAX_MS    3000
#define PERSIST_MS    200               /* chu ky gui PROBE khi cua so = 0 */
#define IDLE_MS       30000             /* server bo giao dich neu im lang qua lau */

enum frame_type {
    T_STR    = 0x01,  /* C->S  xau ky tu                              */
    T_LETTERS = 0x02, /* S->C  xau chu cai cua MD5                    */
    T_DIGITS = 0x03,  /* S->C  xau chu so cua MD5                     */
    T_ERR    = 0x04,  /* S->C  thong bao loi (van ban)                */
    T_FBEGIN = 0x10,  /* C->S  size (8 byte) + ten file               */
    T_DATA   = 0x11,  /* C->S  seq = chi so chunk                     */
    T_ACK    = 0x12,  /* S->C  seq = chunk can nhan tiep, wnd = o trong */
    T_PROBE  = 0x13,  /* C->S  tham do cua so 0                       */
    T_FEND   = 0x14,  /* C->S  seq = tong so chunk                    */
    T_FMD5   = 0x15   /* S->C  32 ky tu hex IN HOA                    */
};

/* Ma tra ve cua recv_frame() */
#define RF_OK     0
#define RF_EOF   (-1)   /* dong ket noi dung ranh gioi khung            */
#define RF_ERR   (-2)   /* loi I/O hoac dut giua chung                  */
#define RF_CRC   (-3)   /* khung nguyen ven ve do dai nhung sai CRC32   */
#define RF_PROTO (-4)   /* len vuot MAX_PAYLOAD                         */

typedef struct {
    uint8_t  type;
    uint8_t  flags;
    uint16_t wnd;
    uint32_t seq;
    uint32_t len;
} frame_hdr_t;

/* ---- Doc/ghi so nguyen big-endian ---- */
static inline void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static inline void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }
static inline void put64(uint8_t *p, uint64_t v) { put32(p, v >> 32); put32(p + 4, v); }
static inline uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) << 16 | get16(p + 2); }
static inline uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) << 32 | get32(p + 4); }

/* ---- common.c ---- */
uint32_t  crc32_calc(const void *data, size_t n, uint32_t crc);
ssize_t   send_all(int fd, const void *buf, size_t n);
ssize_t   recv_all(int fd, void *buf, size_t n);
size_t    build_frame(uint8_t *out, uint8_t type, uint16_t wnd, uint32_t seq,
                      const void *payload, uint32_t len);
int       send_frame(int fd, uint8_t type, uint16_t wnd, uint32_t seq,
                     const void *payload, uint32_t len);
int       recv_frame(int fd, frame_hdr_t *h, uint8_t *payload);
long long now_ms(void);
int       chance(double percent);

typedef struct md5_s md5_t;
md5_t *md5_new(void);
void   md5_update(md5_t *m, const void *data, size_t n);
void   md5_final_hex(md5_t *m, char out[33]);          /* IN HOA, giai phong m */
void   md5_buf_hex(const void *data, size_t n, char out[33]);
int    md5_fd_hex(int fd, char out[33]);               /* bam file, doc theo khoi */
void   split_md5(const char md5[33], char *letters, char *digits);

#endif
