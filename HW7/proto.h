/*
 * proto.h - Hiệp ước frame và API dùng chung cho client/server HW7.
 *
 * Mọi số nguyên trong frame được mã hóa theo thứ tự big-endian. Payload DATA
 * gồm 4 byte sequence number và tối đa CHUNK_SIZE byte dữ liệu tệp.
 */
#ifndef PROTO_H
#define PROTO_H

#include <stddef.h>       /* Kiểu size_t, dùng cho kích thước bộ đệm */
#include <stdint.h>       /* Các kiểu số nguyên có kích thước cố định */
#include <sys/types.h>    /* Kiểu ssize_t dùng cho hàm I/? */

/* ---- Tham số giao thức ---- */
#define CHUNK_SIZE    4096               /* Số byte dữ liệu tối đa trong một D? li?u */
#define HDR_LEN       16                 /* Độ dài ti?u ?? của một frame */
#define MAX_PAYLOAD   (CHUNK_SIZE + 512) /* Kích thước d? li?u t?i lớn nhất */
#define MAX_STR       1024               /* Độ dài tối đa của chuỗi mode 1 */
#define MAX_NAME      255                /* Độ dài tối đa của tên tệp */
#define DEFAULT_BUF   16                 /* Số ô bộ đệm mặc định của server */
#define MAX_INFLIGHT  256                /* Số chunk tối đa đang chờ ACK */
#define RTO_INIT_MS   200                /* RTO b?n đầu, tính bằng mili-giây */
#define RTO_MAX_MS    3000               /* RTO lớn nhất, tính bằng mili-giây */
#define PERSIST_MS    200                /* Ch? kỳ gửi PROBE khi cửa sổ bằng 0 */

/* Loại frame và hướng trao đổi trong giao thức ứng dụng. */
enum frame_type {
    T_STR    = 0x01,  /* C->S  Chuỗi ký tự                           */
    T_LETTERS = 0x02, /* S->C  Chuỗi chữ cái của MD5                  */
    T_DIGITS = 0x03,  /* S->C  Chuỗi chữ số của MD5                   */
    T_ERR    = 0x04,  /* S->C  Thông báo lỗi dạng văn bản             */
    T_FBEGIN = 0x10,  /* C->S  Kích thước (8 byte) + tên tệp           */
    T_DATA   = 0x11,  /* C->S  seq = chỉ số chunk                     */
    T_ACK    = 0x12,  /* S->C  seq = chunk cần nhận tiếp, wnd = ô trống */
    T_PROBE  = 0x13,  /* C->S  Thăm dò khi cửa sổ bằng 0               */
    T_FEND   = 0x14,  /* C->S  seq = tổng số chunk                    */
    T_FMD5   = 0x15   /* S->C  32 ký tự hex IN HOA                    */
};

/* Mã trả về của recv_frame(). */
#define RF_OK     0
#define RF_EOF   (-1)   /* Đóng kết nối đúng ranh giới khung            */
#define RF_ERR   (-2)   /* Lỗi I/? hoặc đứt giữa chừng                  */
#define RF_CRC   (-3)   /* Khung đủ độ dài nhưng sai CRC32              */
#define RF_PROTO (-4)   /* Độ dài vượt MAX_PAYLOAD                     */

typedef struct {
   uint8_t  type;  /* Loại frame theo enum frame_type */
   uint8_t  flags; /* Cờ điều khiển, hiện dành cho mở rộng */
   uint16_t wnd;   /* Số ô bộ đệm còn trống, dùng trong ACK */
   uint32_t seq;   /* Số thứ tự chunk hoặc giá trị điều khiển */
   uint32_t len;   /* Số byte d? li?u t?i của frame */
} frame_hdr_t;

/* ---- Đọc/ghi số nguyên big-endian, không ghi trực tiếp struct lên socket ---- */
/**
* @brief Ghi một số nguyên 16 bit vào bộ đệm theo network byte order.
* @param[out] p Vị trí ghi, phải có ít nhất 2 byte.
* @param[in] v Giá trị 16 bit cần ghi.
*
* Network byte order là thứ tự byte lớn trước (big-endian), được dùng để
* client và server diễn giải frame giống nhau trên mọi kiến trúc máy.
*/
static inline void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
/**
* @brief Ghi một số nguyên 32 bit vào bộ đệm theo network byte order.
* @param[out] p Vị trí ghi, phải có ít nhất 4 byte.
* @param[in] v Giá trị 32 bit cần ghi.
*/
static inline void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }
/**
* @brief Ghi một số nguyên 64 bit vào bộ đệm theo network byte order.
* @param[out] p Vị trí ghi, phải có ít nhất 8 byte.
* @param[in] v Giá trị 64 bit cần ghi.
*/
static inline void put64(uint8_t *p, uint64_t v) { put32(p, v >> 32); put32(p + 4, v); }
/**
* @brief Đọc một số nguyên 16 bit từ bộ đệm theo network byte order.
* @param[in] p Bộ đệm chứa ít nhất 2 byte đã mã hóa big-endian.
* @return Giá trị 16 bit sau khi ghép từ hai byte.
*/
static inline uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
/**
* @brief Đọc một số nguyên 32 bit từ bộ đệm theo network byte order.
* @param[in] p Bộ đệm chứa ít nhất 4 byte đã mã hóa big-endian.
* @return Giá trị 32 bit sau khi ghép từ bốn byte.
*/
static inline uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) << 16 | get16(p + 2); }
/**
* @brief Đọc một số nguyên 64 bit từ bộ đệm theo network byte order.
* @param[in] p Bộ đệm chứa ít nhất 8 byte đã mã hóa big-endian.
* @return Giá trị 64 bit sau khi ghép từ tám byte.
*/
static inline uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) << 32 | get32(p + 4); }

/* ---- API tầng frame và tiện ích dùng chung ---- */
/**
* @brief Tính CRC32 cho một vùng dữ liệu.
* @param[in] data Địa chỉ vùng dữ liệu cần tính; có thể NULL nếu n bằng 0.
* @param[in] n Số byte của vùng dữ liệu.
* @param[in] crc Giá trị CRC ban đầu, cho phép tính tiếp theo nhiều đoạn.
* @return Giá trị CRC32 sau khi xử lý n byte.
*/
uint32_t  crc32_calc(const void *data, size_t n, uint32_t crc);

/**
* @brief Gửi đủ n byte qua socket TCP.
* @param[in] fd File descriptor của socket TCP.
* @param[in] buf Bộ đệm chứa dữ liệu cần gửi.
* @param[in] n Số byte yêu cầu gửi.
* @return Số byte đã gửi; bằng n nếu thành công, -1 nếu gặp lỗi.
*
* Hàm lặp lại send() khi mỗi lần gọi chỉ gửi được một phần dữ liệu hoặc bị
* gián đoạn bởi EINTR. Kết nối bị đóng trước khi gửi đủ n byte được xem là lỗi.
*/
ssize_t   send_all(int fd, const void *buf, size_t n);

/**
* @brief Nhận đủ n byte từ socket TCP.
* @param[in] fd File descriptor của socket TCP.
* @param[out] buf Bộ đệm nhận dữ liệu, phải có ít nhất n byte.
* @param[in] n Số byte yêu cầu nhận.
* @return Số byte đã nhận; bằng n nếu thành công, 0 nếu peer đóng ngay từ đầu,
*         -1 nếu gặp lỗi hoặc kết nối đóng giữa chừng.
*/
ssize_t   recv_all(int fd, void *buf, size_t n);

/**
* @brief Đóng gói header, payload và CRC32 thành một frame.
* @param[out] out Bộ đệm đầu ra, phải đủ sức chứa HDR_LEN + len byte.
* @param[in] type Loại frame theo enum frame_type.
* @param[in] wnd Cửa sổ nhận hoặc số ô bộ đệm còn trống.
* @param[in] seq Số thứ tự của frame/chunk.
* @param[in] payload Payload cần đóng gói; có thể NULL khi len bằng 0.
* @param[in] len Số byte payload, không được vượt MAX_PAYLOAD.
* @return Tổng số byte của frame đã tạo, bằng HDR_LEN + len; trả về 0 nếu
*         len vượt giới hạn giao thức.
*/
size_t    build_frame(uint8_t *out, uint8_t type, uint16_t wnd, uint32_t seq,
                     const void *payload, uint32_t len);

/**
* @brief Tạo và gửi một frame đầy đủ qua socket TCP.
* @param[in] fd File descriptor của socket TCP.
* @param[in] type Loại frame theo enum frame_type.
* @param[in] wnd Giá trị cửa sổ đi kèm frame.
* @param[in] seq Số thứ tự đi kèm frame.
* @param[in] payload Payload cần gửi; có thể NULL khi len bằng 0.
* @param[in] len Số byte payload, không được vượt MAX_PAYLOAD.
* @return 0 nếu gửi thành công, -1 nếu frame không hợp lệ hoặc I/O lỗi.
*/
int       send_frame(int fd, uint8_t type, uint16_t wnd, uint32_t seq,
                    const void *payload, uint32_t len);

/**
* @brief Nhận, kiểm tra và tách một frame từ socket TCP.
* @param[in] fd File descriptor của socket TCP.
* @param[out] h Nơi nhận header đã giải mã.
* @param[out] payload Bộ đệm nhận payload, phải có ít nhất MAX_PAYLOAD byte.
* @return RF_OK nếu frame hợp lệ; RF_EOF nếu peer đóng trước frame mới;
*         RF_ERR nếu lỗi I/O/đóng giữa frame; RF_CRC nếu CRC32 sai;
*         RF_PROTO nếu trường len vượt MAX_PAYLOAD.
*
* CRC được kiểm tra trên header giao thức và payload. Payload vẫn được đọc
* hết trước khi trả RF_CRC để đồng bộ vị trí đọc của stream TCP.
*/
int       recv_frame(int fd, frame_hdr_t *h, uint8_t *payload);

/**
* @brief Lấy thời gian hiện tại theo mili-giây.
* @return Số mili-giây dùng để tính timeout RTO và timer PROBE.
*/
long long now_ms(void);

/**
* @brief Tạo kết quả ngẫu nhiên theo phần trăm để mô phỏng lỗi.
* @param[in] percent Xác s?ất từ 0 đến 100.
* @return 1 nếu sự kiện xảy ra, 0 nếu không xảy ra.
*/
int       chance(double percent);

typedef struct md5_s md5_t;

/**
* @brief Khởi tạo context MD5 mới.
* @return Con trỏ context mới; NULL nếu không cấp phát/khởi tạo được.
*/
md5_t *md5_new(void);

/**
* @brief Nạp thêm dữ liệu vào phép tính MD5.
* @param[in,out] m Context MD5 đã được tạo bởi md5_new().
* @param[in] data Vùng dữ liệu cần băm; có thể NULL nếu n bằng 0.
* @param[in] n Số byte dữ liệu cần nạp.
*/
void   md5_update(md5_t *m, const void *data, size_t n);

/**
* @brief Kết thúc MD5, ghi kết quả thành 32 ký tự hex in hoa.
* @param[in,out] m Context MD5 cần kết thúc; hàm sẽ giải phóng context này.
* @param[out] out Bộ đệm phải có ít nhất 33 byte, gồm 32 ký tự và '\0'.
*/
void   md5_final_hex(md5_t *m, char out[33]);

/**
* @brief Tính MD5 trực tiếp cho một vùng bộ nhớ.
* @param[in] data Vùng dữ liệu cần băm; có thể NULL nếu n bằng 0.
* @param[in] n Số byte dữ liệu.
* @param[out] out Bộ đệm phải có ít nhất 33 byte, kết quả hex in hoa.
*/
void   md5_buf_hex(const void *data, size_t n, char out[33]);

/**
* @brief Tính MD5 cho toàn bộ nội d?ng tệp đã mở sẵn.
* @param[in] fd File descriptor đang trỏ đến tệp cần đọc.
* @param[out] out Bộ đệm phải có ít nhất 33 byte, kết quả hex in hoa.
* @return 0 nếu đọc và băm thành công, -1 nếu read() hoặc khởi tạo MD5 lỗi.
*/
int    md5_fd_hex(int fd, char out[33]);

/**
* @brief Tách chuỗi MD5 thành nhóm chữ cái và nhóm chữ số.
* @param[in] md5 Chuỗi gồm 32 ký tự hex và ký tự kết thúc '\0'.
* @param[out] letters Bộ đệm nhận các ký tự A-F và ký tự '\0'.
* @param[out] digits Bộ đệm nhận các ký tự 0-9 và ký tự '\0'.
*
* Mỗi bộ đệm đầu ra nên có ít nhất 33 byte để đủ sức chứa các ký tự kết quả.
*/
void   split_md5(const char md5[33], char *letters, char *digits);

#endif
