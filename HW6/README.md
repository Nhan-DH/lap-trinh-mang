# BÀI TẬP TUẦN 6 – IT4062: LẬP TRÌNH MẠNG
## ĐỀ BÀI CHI TIẾT & HƯỚNG DẪN TRIỂN KHAI ỨNG DỤNG TCP SOCKET
### Đề tài: Xử lý xâu MD5 tách chữ/số & Truyền nhận tệp kiểm tra toàn vẹn

---

## 1. Đề bài tường minh (Assignment Specification)

Xây dựng ứng dụng Client - Server sử dụng **giao thức TCP Socket** trong ngôn ngữ C/C++ trên hệ điều hành Linux với các yêu cầu kỹ thuật cụ thể sau:

### 1.1. Phía Server
1. **Tham số dòng lệnh & Khởi động**:
   - Chạy với cú pháp: `./server PortNumber` (ví dụ: `./server 5500`).
   - Kiểm tra chặt chẽ tham số: thiếu tham số hoặc số hiệu cổng không hợp lệ (không phải số nguyên hoặc nằm ngoài dải `1 - 65535`) phải in hướng dẫn cú pháp và thoát với mã lỗi khác 0.
   - Phục vụ **đồng thời nhiều client** bằng mô hình đa tiến trình con (`fork()` cho mỗi client kết nối).
2. **Nhận và xử lý dữ liệu từ Client**:
   - **Chế độ 1 - Nhận xâu ký tự**:
     - Kiểm tra xâu nhận được: xâu hợp lệ **chỉ được chứa các ký tự chữ cái (`a-z`, `A-Z`) và chữ số (`0-9`)**.
     - Nếu xâu chứa ký tự không hợp lệ (dấu cách, ký tự đặc biệt như `@`, `#`, `$`, ...): gửi lại thông báo lỗi `Error: String contains non-alphanumeric characters` về cho client; server không tính MD5; phiên làm việc của client vẫn tiếp tục bình thường.
     - Nếu xâu hợp lệ: Encode xâu bằng thuật toán **MD5 (In hoa)**. Sau đó tách chuỗi băm kết quả thành 2 xâu:
       - Một xâu **chỉ chứa các ký tự chữ cái** (`Letters`).
       - Một xâu **chỉ chứa các ký tự chữ số** (`Digits`).
     - Hiển thị trên console server: xâu nhận được, mã MD5, xâu chữ cái và xâu chữ số.
     - Gửi 2 xâu này về cho client bằng **hai lần gọi hàm gửi riêng biệt** (`send`) để client nhận và đối chiếu.
   - **Chế độ 2 - Nhận tệp dữ liệu (File Transfer)**:
     - Lưu tệp nhận được vào thư mục `received_files/` theo định dạng đặt tên:  
       `received_files/<IP>_<port>_<original_filename>`  
       *(Ví dụ client `127.0.0.1:50998` gửi file `sample_text.txt` thì lưu tại `received_files/127.0.0.1_50998_sample_text.txt`)*.
     - Hiển thị thông tin tiếp nhận trên màn hình server:
       - Đường dẫn lưu: `Saved at    : received_files/<IP>_<port>_<original_filename>`
       - Kích thước: `File size   : <size> bytes`
     - Nếu là **file văn bản (text file)**: hiển thị toàn bộ nội dung file trên màn hình server.
     - Hiển thị mã băm MD5 của file (`MD5         : <hash>`).
     - Gửi mã băm **MD5 của file (In hoa, không tách xâu)** về cho client dùng để đối chiếu tính toàn vẹn.

---

### 1.2. Phía Client
1. **Tham số dòng lệnh & Kết nối**:
   - Chạy với cú pháp: `./client IPAddress PortNumber` (ví dụ: `./client 127.0.0.1 5500`).
   - Kiểm tra địa chỉ IPv4 hợp lệ, cổng hợp lệ; nếu kết nối thất bại phải báo lỗi rõ ràng.
2. **Giao diện Menu tương tác**:
   ```text
   MENU
   ----------------------------------- 
   1. Send any string
   2. Send file content
   3. Exit
   Your choice: 
   ```
3. **Chức năng Chế độ 1 (`Send any string`)**:
   - Cho phép người dùng nhập xâu bất kỳ từ bàn phím và gửi cho server.
   - Chức năng lặp lại cho tới khi người dùng **nhập xâu rỗng (nhấn Enter)** thì quay lại MENU.
   - Nhận phản hồi từ server:
     - Nếu server báo lỗi: in thông báo lỗi.
     - Nếu server trả về 2 xâu: in xâu chữ cái (`Letters: ...`) và xâu chữ số (`Digits: ...`).
     - **Đối chiếu**: Client tự tính mã MD5 cục bộ của xâu, tách chữ/số và so sánh với phản hồi server nhận được; in `Check: Match` nếu trùng khớp, ngược lại in `Check: Mismatch`.
4. **Chức năng Chế độ 2 (`Send file content`)**:
   - Nhập đường dẫn file từ bàn phím.
   - Kiểm tra điều kiện:
     - Tệp có tồn tại trên máy không?
     - Có đúng là tệp thông thường không (`S_ISREG`, từ chối thư mục)?
     - Kích thước tệp có **dưới 10MB** không?
   - Đọc nội dung tệp bằng `fread` và gửi dần (`send`) theo từng khối (chunk 8KB) lên server.
   - Sau khi gửi xong, nhận mã băm MD5 từ server trả về.
   - **Đối chiếu**: Client tự tính mã băm MD5 từ tệp gốc khi gửi và so sánh với mã MD5 nhận từ server; in `Check: Match` nếu khớp, ngược lại `Check: Mismatch`.
   - Quay lại MENU chính sau khi hoàn thành.
5. **Thoát ứng dụng (Lựa chọn `3` hoặc `EOF`)**:
   - In `Connection closed. Goodbye!` và kết thúc với mã lỗi 0 (`exit code 0`).

---

## 2. Cấu trúc thư mục dự án

```text
HW6/
├── Makefile             # Quản lý biên dịch make, make clean, make submit, make pack-tucham
├── protocol.h / .c      # Giao thức đóng khung header trên TCP (send_all, recv_all, recv_line)
├── md5.h / md5.c        # Thư viện tính MD5 thuần C chuẩn RFC 1321
├── server.c             # Mã nguồn TCP Server xử lý xâu và tệp
├── client.c             # Mã nguồn TCP Client với Menu và cơ chế tự đối chiếu MD5
├── sample_input.txt     # Dữ liệu nhập mẫu cho client
├── sample_output.txt    # Nhật ký log mẫu chuẩn đối chiếu giữa client & server
├── sample_text.txt      # Tệp văn bản mẫu
├── sample_image.png     # Tệp ảnh / tệp nhị phân mẫu
├── test_hw6.py          # Bộ test tự động kiểm thử toàn diện
├── grade_hw6.sh         # Script chấm điểm tự động thang 10
├── grade_hw6            # Bộ chấm điểm nhị phân biên dịch sẵn (bảo mật, chống lộ kịch bản test)
├── HUONG_DAN_TU_CHAM.md # Hướng dẫn sinh viên tự chấm điểm
└── README.md
```

---

## 3. Giao thức ứng dụng (TCP Framing Protocol)

Do TCP là luồng byte không có ranh giới bản tin, giao thức ứng dụng sử dụng header kết thúc bằng ký tự xuống dòng `\n`:

| Chiều | Cú pháp Header | Nội dung truyền tiếp theo | Ý nghĩa |
| :--- | :--- | :--- | :--- |
| **C → S** | `STR <len>\n` | `<len>` byte chuỗi ký tự | Gửi chuỗi ký tự |
| **C → S** | `FILE <size> <name>\n` | `<size>` byte dữ liệu file | Gửi file (truyền theo từng chunk) |
| **S → C** | `<letters>\n` rồi `<digits>\n` | Không có (2 lần gọi `send`) | Trả về 2 xâu tách từ MD5 (Chế độ 1) |
| **S → C** | `<MD5 32 ký tự in hoa>\n` | Không có | Trả về mã MD5 của file (Chế độ 2) |
| **S → C** | `Error: <thông báo>\n` | Không có | Thông báo lỗi khi xâu/file sai quy cách |

---

## 4. Bảng Kịch Bản Kiểm Thử & Đối Chiếu Input / Output Toàn Diện

### 4.1. Khởi động & Tham số dòng lệnh

| Thành phần | Lệnh thực thi / Đầu vào | Hiển thị / Phản hồi | Đánh giá |
| :--- | :--- | :--- | :--- |
| **Server chuẩn** | `./server 5500` | `TCP server started at port 5500. Waiting for clients...` | Khởi động thành công |
| Server sai cổng | `./server` hoặc `./server abc` | `Usage: ./server PortNumber` (mã lỗi ≠ 0) | Bắt lỗi đúng |
| **Client chuẩn** | `./client 127.0.0.1 5500` | In `MENU` (1. Send any string, 2. Send file content, 3. Exit) | Kết nối thành công |
| Client sai IP/Port | `./client 127.0.0.1 99999` | Báo lỗi cổng không hợp lệ và thoát với mã lỗi ≠ 0 | Bắt lỗi đúng |

---

### 4.2. Chế độ 1: Gửi xâu (MD5 In hoa → Tách xâu chữ cái & chữ số)

Người dùng chọn `1` tại Menu, sau đó nhập từng xâu ký tự:

| STT | Người dùng nhập (Client Input) | Hiển thị tại Client (Client Output) | Hiển thị tại Server (Server Output) | Mô tả quy tắc |
| :---: | :--- | :--- | :--- | :--- |
| **1** | `hust` | `Letters: EFBFACBEABCB`<br>`Digits : 59814005382377716634`<br>`Check  : Match` | `Received string: "hust"`<br>`MD5          : E5F981B4FA005CB38EA23777166BC34B`<br>`Letters      : EFBFACBEABCB`<br>`Digits       : 59814005382377716634` | Xâu chữ cái thường. Server gửi 2 lần riêng biệt; client đối chiếu khớp. |
| **2** | `1a2b3cd` | `Letters: BEBFBCCFCBBDF`<br>`Digits : 2731308865246901993`<br>`Check  : Match` | `Received string: "1a2b3cd"`<br>`MD5          : 2B7E3B1FB3C0C8F86C5246B901B99DF3`<br>`Letters      : BEBFBCCFCBBDF`<br>`Digits       : 2731308865246901993` | Xâu gồm chữ cái và chữ số |
| **3** | `123` | `Letters: CBACBBDB`<br>`Digits : 202962590759640715223470`<br>`Check  : Match` | `Received string: "123"`<br>`MD5          : 202CB962AC59075B964B07152D234B70`<br>`Letters      : CBACBBDB`<br>`Digits       : 202962590759640715223470` | Xâu chỉ chứa chữ số |
| **4** | `abcd` | `Letters: EFCCEEFCDEFF`<br>`Digits : 27144727939532427331`<br>`Check  : Match` | `Received string: "abcd"`<br>`MD5          : E2FC714C4727EE9395F324CD2E7F331F`<br>`Letters      : EFCCEEFCDEFF`<br>`Digits       : 27144727939532427331` | Xâu chỉ chứa chữ cái |
| **5** | `IT4062` | `Letters: CBEEAAEDEEBE`<br>`Digits : 04236685079569368851`<br>`Check  : Match` | `Received string: "IT4062"`<br>`MD5          : 0C423668507BE9E5A6A93E6D8E8E5B1E`<br>`Letters      : CBEEAAEDEEBE`<br>`Digits       : 04236685079569368851` | Xâu chữ cái in hoa + số |
| **6** | `Ab15CD$` | `Error: String contains non-alphanumeric characters` | `Received string: "Ab15CD$"`<br>`-> Error: String contains non-alphanumeric characters. Error message sent.` | Chứa ký tự `$`, báo lỗi, không tính MD5 |
| **7** | `hello world` | `Error: String contains non-alphanumeric characters` | `Received string: "hello world"`<br>`-> Error: String contains non-alphanumeric characters. Error message sent.` | Chứa dấu cách, báo lỗi, không tính MD5 |
| **8** | *(Nhấn Enter rỗng)* | Quay lại `MENU` chính | Duy trì kết nối TCP | Kết thúc Chế độ 1 |

---

### 4.3. Chế độ 2: Gửi nội dung tệp (Lưu tệp, hiển thị nội dung text & đối chiếu MD5)

Người dùng chọn `2` tại Menu, sau đó nhập đường dẫn tệp:

| STT | Loại tệp / Đầu vào | Hiển thị tại Client (Client Output) | Hiển thị & Xử lý tại Server (Server Output) | Ý nghĩa / Quy tắc |
| :---: | :--- | :--- | :--- | :--- |
| **1** | **File Text**: `sample_text.txt`<br>*(130 bytes)* | • `Sent 130 bytes of file 'sample_text.txt' to server.`<br>• `MD5: 575D5A537A860877D481B00CDAAAD975`<br>• `Check: Match` | • `Received file: "sample_text.txt"`<br>• `Saved at    : received_files/127.0.0.1_50998_sample_text.txt`<br>• `File size   : 130 bytes`<br>• Hiển thị nội dung text giữa 2 viền `FILE CONTENT`<br>• `MD5         : 575D5A537A860877D481B00CDAAAD975` | Tệp văn bản hiển thị nội dung và lưu đúng quy chuẩn. |
| **2** | **File Nhị phân**: `server`<br>*(35096 bytes)* | • `Sent 35096 bytes of file 'server' to server.`<br>• `MD5: 32A4FE8205C8EFDAA8D003CBB13532A1`<br>• `Check: Match` | • `Received file: "server"`<br>• `Saved at    : received_files/127.0.0.1_50998_server`<br>• `File size   : 35096 bytes`<br>• Không in nội dung nhị phân ra màn hình<br>• `MD5         : 32A4FE8205C8EFDAA8D003CBB13532A1` | Tệp nhị phân được lưu toàn vẹn, không làm hỏng terminal. |
| **3** | **Tệp không tồn tại**: `non_existent.txt` | `Error: File 'non_existent.txt' not found.` | Không gửi yêu cầu lên server | Kiểm tra hợp lệ phía Client |
| **4** | **Thư mục**: `/home/nqkpr/LTM` | `Error: '/home/nqkpr/LTM' is not a regular file.` | Không gửi yêu cầu lên server | Client kiểm tra bằng `S_ISREG` |
| **5** | **Tệp ≥ 10MB**: `file_10mb.bin` | `Error: File size must be less than 10MB.` | Bị từ chối nếu có gian lận | Giới hạn dung lượng theo đề bài |

---

### 4.4. Thoát ứng dụng

| Thao tác tại Client | Hiển thị tại Client | Phản hồi tại Server |
| :--- | :--- | :--- |
| Chọn `3` hoặc gửi `EOF` (`Ctrl+D`) | `Connection closed. Goodbye!` | `[<IP>:<port>] Client disconnected.` |

---

## 5. Biên dịch & Hướng dẫn chạy thử

```bash
make                                        # Biên dịch ./server và ./client
./server 5500                               # Chạy server ở cổng 5500
./client 127.0.0.1 5500                     # Chạy client kết nối tới server
./client 127.0.0.1 5500 < sample_input.txt  # Chạy kịch bản tự động mẫu
```

### Chấm điểm tự động
```bash
./grade_hw6                                 # Chạy file chấm điểm nhị phân tự động
make test                                   # Tương đương ./grade_hw6 ./server ./client
python3 test_hw6.py                         # Bộ kiểm thử Python
```

---

## 6. Đóng gói nộp bài
```bash
make submit HOTEN=NguyenVanA MSSV=20161234   # Tạo file nén NguyenVanA_20161234_HW6.zip
./grade_hw6 NguyenVanA_20161234_HW6.zip     # Tự kiểm tra lại file zip trước khi nộp
```
