# HƯỚNG DẪN TỰ CHẤM ĐIỂM BÀI TẬP HW6 (IT4062 - LẬP TRÌNH MẠNG)

Gói công cụ giúp sinh viên tự kiểm thử bài HW6 (TCP Socket: Gửi xâu → MD5 in hoa tách chữ cái/chữ số; Gửi file → hiển thị nội dung/ảnh và trả về MD5) trước khi nộp. Toàn bộ các thông điệp hiển thị và giao diện dòng lệnh đã được chuẩn hóa sang **tiếng Anh**.

> **Lưu ý:** Gói tự chấm **chỉ chứa file thực thi mẫu (`server_sample`, `client_sample`) và file chấm nhị phân (`grade_hw6`)**, **không chứa mã nguồn script plain-text**.

---

## 1. Thành phần gói tự chấm
- `grade_hw6`: File thực thi nhị phân (ELF binary đã biên dịch và strip symbol) chấm tự động thang 10.0 (chấm file zip nộp bài, file thực thi hoặc tự nhận diện). Toàn bộ nội dung hiển thị sang tiếng Anh, mã kiểm thử được mã hóa nhị phân an toàn.
- `server_sample`, `client_sample`: File thực thi mẫu đã biên dịch để chạy thử và đối chiếu.
- `sample_input.txt`, `sample_output.txt`, `sample_text.txt`, `sample_image.png`: Dữ liệu kiểm thử mẫu bằng tiếng Anh.
- `README.md`: Tài liệu kỹ thuật chi tiết (giao thức, bảng đối chiếu Input/Output).

---

## 2. Cách sử dụng

```bash
chmod +x grade_hw6
./grade_hw6                                # Tự động tìm *_HW6.zip / Makefile / server+client
./grade_hw6 NguyenVanA_20161234_HW6.zip    # Chấm trực tiếp file zip trước khi nộp
./grade_hw6 ./server ./client              # Chấm 2 file thực thi tự biên dịch
./grade_hw6 ./server_sample ./client_sample # Chấm kiểm tra 2 file mẫu
```

Chạy thủ công với dữ liệu mẫu:
```bash
./server_sample 5000                                   # Terminal 1
./client_sample 127.0.0.1 5000 < sample_input.txt      # Terminal 2
```

---

## 3. Quy ước hiển thị & tương tác của bài làm

Bộ chấm tự động tương tác với chương trình client qua luồng `stdin` (theo đúng MENU đề bài) và đối chiếu kết quả hiển thị tại client cũng như log tại server:

| Mục | Quy ước |
| :--- | :--- |
| **MENU** | Chọn `1` để gửi xâu; chọn `2` để gửi file; chọn `3` để thoát |
| **Chế độ 1** | Gửi xâu hợp lệ: Server in xâu chữ cái và xâu chữ số; gửi 2 xâu về client qua 2 lần `send` riêng biệt. Client in `Letters: ...` và `Digits: ...`. Nhập dòng rỗng (Enter) để quay lại MENU. |
| **Bắt lỗi xâu** | Xâu chứa ký tự đặc biệt hoặc dấu cách: Server/Client báo lỗi có chứa từ `Error` hoặc `Invalid`. Phiên làm việc vẫn tiếp tục. |
| **Chế độ 2** | Nhập đường dẫn tệp: Client kiểm tra tồn tại, tệp thường và kích thước dưới 10MB. Gửi xong quay lại MENU. |
| **File Text** | Server in nội dung text ra console. In thông tin `Saved at`, `File size` và `MD5`. Client nhận mã MD5 in hoa và đối chiếu (`Check: Match`). |
| **File Nhị phân** | Server lưu file tại `received_files/<IP>_<port>_<original_filename>`, hiển thị `File size` và `MD5`, không in nhị phân làm hỏng terminal. Client nhận mã MD5 và đối chiếu (`Check: Match`). |
| **Kết thúc (EOF)** | Khi hết dữ liệu `stdin`, client thoát với mã lỗi 0 (`exit code 0`). |

---

## 4. Thang điểm đánh giá (Tổng điểm: 10.0)

| Test | Điểm | Tiêu chí đánh giá |
| :--- | :---: | :--- |
| **1. Compilation & Executables** | **1.0** | Makefile biên dịch sinh đúng `server` và `client` không lỗi |
| **2. Command Line Arguments** | **1.0** | Xử lý lỗi thiếu tham số, cổng sai (> 65535, ký tự), IP không hợp lệ, cổng chưa mở |
| **3. Mode 1: Uppercase MD5 & Split Strings** | **2.0** | Server tính MD5 in hoa, tách xâu chữ cái & chữ số, hiển thị tại server và gửi 2 xâu về client qua 2 lệnh send riêng biệt; client tự đối chiếu |
| **4. Mode 1: Reject Non-alphanumeric** | **1.0** | Xâu có ký tự lạ/dấu cách bị báo lỗi; server không tính MD5; phiên TCP tiếp tục bình thường |
| **5. Mode 2: Text File Transfer** | **2.0** | Server hiển thị toàn vẹn nội dung file text, kích thước và mã MD5; client nhận đúng MD5 in hoa |
| **6. Mode 2: File Saving & Size & MD5** | **1.0** | Server lưu đúng `received_files/<IP>_<port>_<name>`, hiển thị kích thước, mã MD5 toàn vẹn; không làm hỏng terminal với file nhị phân |
| **7. Mode 2: File Path Validation** | **1.0** | Từ chối tệp không tồn tại, thư mục, tệp đúng 10MB; ứng dụng không bị crash |
| **8. Large Binary File Transfer (~9.5MB)** | **0.5** | Truyền tệp nhị phân lớn toàn vẹn qua TCP theo từng chunk, MD5 khớp |
| **9. Concurrent Multi-client Handling** | **0.5** | Server phục vụ đồng thời nhiều client qua `fork()` |

> **Lưu ý:** Đạt điểm **10.0 / 10.0** là bài làm hoàn hảo và sẵn sàng nộp bài.
