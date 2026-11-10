# HƯỚNG DẪN TỰ CHẤM BÀI TẬP TUẦN 8 (IT4062)

Gói này giúp sinh viên tự kiểm thử bài làm **trước khi nộp**. Bộ chấm chạy đúng các test case được mô tả trong `DE_BAI.md`, mục 6, và cho điểm tự động trên thang 10.

---

## 1. Yêu cầu

- Linux hoặc WSL2, có `python3` (3.8 trở lên), `gcc`, `make`.
- Thư viện OpenSSL dev: `sudo apt install libssl-dev`.
- Lệnh `openssl` (có sẵn trên hầu hết hệ thống Linux). Bộ chấm dùng nó để mã hóa/giải mã AES khi nói chuyện với server.
- Chỉ dùng thư viện chuẩn của Python, không cần cài thêm gói nào.

---

## 2. Cách chạy

Giải nén gói, đứng trong thư mục chứa `grade.py`, rồi chạy:

```bash
# Chấm đúng file zip sẽ nộp (khuyến nghị)
python3 grade.py --zip NguyenVanA_20161234_HW8.zip

# Hoặc chấm một thư mục bài làm (phải có Makefile)
python3 grade.py --dir ../bai_lam_cua_toi

# Chỉ chạy một vài test để kiểm tra nhanh, ví dụ D1 và F2
python3 grade.py --dir ../bai_lam_cua_toi --only D1,F2
```

Bộ chấm sao chép bài sang thư mục tạm, chạy `make` ở đó, rồi mới chạy test. **Bài gốc không bị sửa.**

Bài đúng chạy đầy đủ trong khoảng 10 giây. Với bài bị treo hoặc sai nhiều, mỗi test có thể chờ tới timeout (3 đến 5 giây), nên tổng có thể lên khoảng 100 giây.

---

## 3. Đọc kết quả

Ví dụ một lần chạy có một test không đạt:

```
 [A1] (0.50) make tạo đủ ./server và ./client ............ ĐẠT
 [B1] (0.25) ./server không tham số, cổng sai (99999, abc) → thoát mã khác 0 ........ ĐẠT
 [C1] (0.30) REGISTER hợp lệ → OK; account.txt có dòng 'username sha256(password)' ........ KHÔNG ĐẠT
      -> account.txt không đúng định dạng 'alice <sha256(pass)>'. Nội dung hiện có: [('alice', 'secret123')]
      [log server, 8 dòng cuối]
      | Server dang lang nghe tren cong 38843
...
 Tổng điểm tự động: 9.70 / 10.00
```

- Mỗi dòng `[Mã]` cho biết test và điểm; `ĐẠT` hoặc `KHÔNG ĐẠT`.
- Dòng `->` là lý do không đạt, bám sát một kỳ vọng cụ thể trong đề bài.
- Khối `[log server]` là 8 dòng cuối trong log của server lúc test đó, để bạn tìm nguyên nhân.

---

## 4. Các lỗi thường gặp

| Triệu chứng | Nguyên nhân thường gặp |
|---|---|
| A1 không đạt, log có `openssl/evp.h: No such file` | Chưa cài `libssl-dev`. Chạy `sudo apt install libssl-dev`. |
| A1 không đạt, log có `undefined reference` | Thiếu `-lcrypto` trong Makefile, hoặc thiếu file `.c` trong danh sách biên dịch. |
| Client không in dòng `[OK]`, `[MSG]`, `[FILE]` dù đã gửi lệnh | Chưa `fflush(stdout)` sau mỗi dòng in ra. Khi chấm, stdout là pipe nên bị đệm. |
| Server "không phản hồi" ở mọi test | Server bị treo khi khởi động, hoặc `accept` không vòng lặp, hoặc không gửi phản hồi bằng `send_msg`. Xem log server trong báo cáo. |
| D1 báo nhiều hơn 1 `OK` | Thiếu mutex quanh việc kiểm tra và ghi `account.txt`. Hai luồng cùng thấy username chưa tồn tại. |
| H1, F2 báo server thoát | Ghi vào socket đã đóng làm chết tiến trình. Phải bỏ qua `SIGPIPE` hoặc dùng `MSG_NOSIGNAL`. |
| C6 không đạt sau `QUIT` | Chưa gỡ user khỏi danh sách online khi kết nối kết thúc. |

---

## 5. Lưu ý

- Bộ chấm chỉ kiểm tra hành vi bên ngoài. Các tiêu chí về mutex, quản lý bộ nhớ và chất lượng mã nguồn do GV đánh giá riêng, nên bài đạt đủ điểm tự động vẫn có thể bị trừ khi mã nguồn chưa tốt.
- Bộ chấm dùng server và client **của bạn**, nên chỉ phát hiện được lỗi đúng với giao thức trong đề bài. Hãy đọc kỹ mục 3 và mục 5 của đề.
- Nếu một test không đạt mà bạn cho là bộ chấm sai, hãy chụp lại dòng `->` và báo GV, đừng sửa bài để khớp với bộ chấm trong khi bài đúng theo đề.
