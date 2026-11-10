# IT4062 - HW8: TCP Chat đa luồng (C / pthread / OpenSSL)

## 1. Đã đọc và bám sát DE_BAI.md

Đây là **bản hoàn thiện từ khung mã nguồn do đề bài cung cấp**, không phải bản thiết kế khác. Các file `common.h`, `crypto.c`, `net.c` được **giữ nguyên byte**, vì đề yêu cầu không thay đổi hằng số/chữ ký hàm và quy cách mã hóa.

- `server.c`: hoàn thiện các TODO 1–12, thêm kiểm tra UTF-8 của HEADER và các chú thích dạng `@brief`, `@input`, `@output`, `@mutex`.
- `client.c`: hoàn thiện các TODO 1–4, có giải thích tham số, dữ liệu trả về, đồng bộ Mutex và quản lý bộ nhớ.
- `Makefile`: biên dịch đúng hai file thực thi tên `server` và `client`.
- `crypto.c`: **AES-256-CBC**, IV 16 byte ngẫu nhiên mỗi frame; SHA-256 mật khẩu theo đúng đề.
- `net.c`: định dạng `[4-byte LEN big endian][16-byte IV][AES-CBC ciphertext]`.

> Lưu ý: SHA-256 không salt và khóa AES dùng chung cố định là yêu cầu **cụ thể của DE_BAI.md để tương thích với bộ chấm**. Đây là thiết kế phục vụ bài tập, không phải cách lưu mật khẩu hoặc quản lý khóa an toàn cho sản phẩm thực tế. Không thay bằng Argon2id hay AES-GCM vì sẽ không còn tương thích với giao thức bài tập.

## 2. Môi trường

Linux hoặc WSL2 trên Windows, GCC, Make, OpenSSL development headers:

```bash
sudo apt update
sudo apt install build-essential libssl-dev
```

## 3. Biên dịch

Trong thư mục chứa Makefile:

```bash
make
```

Kết quả là `./server` và `./client`.

Xóa file thực thi:

```bash
make clean
```

## 4. Chạy thử 3–5 Client

**Terminal 1 — Server**

```bash
./server 5500
```

**Terminal 2 — Alice**

```bash
./client 127.0.0.1 5500
/register alice secret123
/login alice secret123
/list
/msg bob Xin chao Bob!
/file bob anh.png
/quit
```

**Terminal 3 — Bob**

```bash
./client 127.0.0.1 5500
/register bob secret123
/login bob secret123
/list
/msg alice Chao Alice!
```

Có thể mở thêm Terminal 4–6 với tên `carol`, `david`, `eric`. **Cần đăng ký/đăng nhập Bob trước khi Alice gửi tin cho Bob.** File `anh.png` phải tồn tại trong thư mục hiện hành của Alice. Ảnh Bob nhận được lưu tại `downloads/alice_anh.png` trong thư mục hiện hành của Bob.

## 5. Lệnh và đầu ra

| Client nhập | Server nhận (HEADER) | Ý nghĩa |
|---|---|---|
| `/register user pass` | `REGISTER user pass` | Ghi `account.txt` với mật khẩu SHA-256 hex |
| `/login user pass` | `LOGIN user pass` | Online; chặn đăng nhập trùng |
| `/list` | `LIST` | Trả `OK USERS ...` |
| `/msg user text` | `MSG user text` | Forward `MSG sender text` |
| `/file user path.png` | `FILE user file.png size` + BODY | Forward ảnh nhị phân, giới hạn 10 MiB |
| `/quit` | Không bắt buộc gửi QUIT | Client thoát với mã 0, đóng kết nối |

Phản hồi hiển thị đúng mẫu đề: `[OK] ...`, `[ERR 403] ...`, `[ERR] ...`, `[MSG] alice: ...`, `[FILE] alice: anh.png (204800 bytes) -> downloads/alice_anh.png`.

Server hỗ trợ `QUIT` trong giao thức và trả `OK BYE` rồi đóng kết nối; `/quit` ở chương trình Client kết thúc tại phía Client theo mục 5.2.

## 6. Những hàm quan trọng: Brief / Input / Output

### Server

| Hàm | Brief | Input | Output |
|---|---|---|---|
| `send_to` | Khóa mutex ghi và gửi một frame | `conn_t *`, `header`, `body`, `blen` | `0` hoặc `-1` |
| `account_find` | Đọc account.txt và tìm Username | `user`, `hash_out[65]` | `1` tìm thấy, `0` không |
| `account_add` | Thêm dòng account.txt | `user`, `sha256_hex` | `0` hoặc `-1` |
| `online_find` | Tìm tài khoản online | `user` | Con trỏ `conn_t *` hoặc `NULL` |
| `online_add` | Đăng ký conn_t vào mảng online | `conn_t *` | `0` hoặc `-1` |
| `online_remove` | Gỡ con trỏ trước khi giải phóng | `conn_t *` | `void` |
| `cmd_register` | Kiểm tra cú pháp, băm pass, lưu account | `conn_t *`, `params` | Phản hồi OK/ERR |
| `cmd_login` | Xác minh hash và chặn đăng nhập trùng | `conn_t *`, `params` | Phản hồi OK/ERR |
| `cmd_list` | Liệt kê người dùng đang online | `conn_t *` | `OK USERS ...` |
| `cmd_msg` | Chuyển tin nhắn văn bản | sender, recipient, text | `OK SENT` / `ERR` |
| `cmd_file` | Chuyển tiếp ảnh nguyên bytes | sender, recipient, filename, body | `OK SENT` / `ERR` |
| `conn_thread` | Vòng lặp nhận frame cho một kết nối | `conn_t *` từ `pthread_create` | `NULL` + cleanup |

### Client

| Hàm | Brief | Input | Output |
|---|---|---|---|
| `send_cmd` | Khóa `g_wlock` và gửi frame | `header`, `body`, `blen` | `0` hoặc `-1` |
| `handle_incoming` | Hiển thị phản hồi / lưu file | `plain`, `plen` | In stdout / ghi downloads |
| `recv_thread` | Nhận bất đồng bộ với stdin | Không dùng `arg` | In phản hồi; lỗi kết nối thoát 1 |
| `handle_command` | Phân tích các lệnh `/...` | Chuỗi dòng lệnh có thể sửa | `0` tiếp tục, `1` thoát |

### Mutex

- `g_lock` (Server): bảo vệ **cả `account.txt` và `g_online`**; tránh tranh chấp khi đăng ký đồng thời. Đặc biệt, kiểm tra user và ghi file diễn ra trong *cùng* critical section.
- `c->wlock` (Server): bảo vệ toàn bộ một frame khi nhiều worker có thể gửi tới cùng một client.
- `g_wlock` (Client): bảo vệ frame gửi đi.
- `g_plock` (Client): bảo vệ `printf` và `fflush(stdout)` để không bị đan xen.

**Thứ tự mutex:** khi Server cần cả hai, giữ `g_lock` trước rồi mới tới `c->wlock`; không có nhánh ngược lại. `recv_msg` không giữ `g_lock`.

## 7. Kiểm tra đã thực hiện

Tại môi trường Linux hiện hành, đã xác nhận:

- `make` tạo được `server` và `client` không có compiler warning.
- 5 client đăng ký, đăng nhập, LIST, gửi chat vòng tròn, lưu ảnh 200 KiB đúng byte.
- 12 vòng × 10 kết nối cùng REGISTER một username: mỗi vòng đúng 1 OK và 9 ERR 409.
- 100 tin nhắn liên tục: đầy đủ và giữ thứ tự.
- Chuyển tiếp ảnh nhị phân 300 KiB: BODY nhận trùng hoàn toàn.
- Frame không có `\\n`, header UTF-8 sai, size file sai, file >10 MiB, ciphertext lỗi, frame >12 MiB: phản hồi theo mã và Server tiếp tục hoạt động.

Đây là kiểm thử riêng đối với các chức năng cốt lõi, **không phải xác nhận đã chạy bộ chấm chính thức** `HW8_TuChamDiem.zip` (bộ chấm đó chưa được cung cấp).

## 8. Đóng gói nộp

ZIP nộp chứa đúng các file mã nguồn `server.c`, `client.c`, `common.h`, `crypto.c`, `net.c`, `Makefile` (kèm README tùy ý). **Không** cần nộp binary, `account.txt`, ảnh trong downloads hay đối tượng `.o`.

Đổi tên file ZIP sang `HotenSV_MSSV_HW8.zip` với họ tên không dấu và MSSV thật trước khi nộp.


## Ghi chú học code chi tiết

Đọc `GIAI_THICH_CODE.md` để hiểu từng bước socket(), bind(), listen(), accept(), connect(), send()/recv(), network byte order, mutex và luồng frame. `server.c` / `client.c` có comment dạng `@brief/@input/@output` và `Step N` ngay trong hàm. Ba file giáo viên cung cấp (`common.h`, `crypto.c`, `net.c`) không thay đổi.
