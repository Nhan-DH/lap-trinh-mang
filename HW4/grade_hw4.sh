./client 127.0.0.1 5500#!/usr/bin/env bash
# ==============================================================================
# Script chấm điểm và đánh giá tự động bài tập HW4 (Lập trình mạng - IT4062)
# Chương trình UDP Socket Chat 2 Clients & Kiểm tra tính hợp lệ của chuỗi
#
# Cách sử dụng:
#   ./grade_hw4.sh                             (Tự động nhận diện bài làm/zip)
#   ./grade_hw4.sh NguyenVanA_20161234_HW4.zip (Chấm file zip nộp bài)
#   ./grade_hw4.sh ./server ./client           (Chấm trực tiếp file thực thi)
# ==============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m' # No Color

TOTAL_SCORE=0.0
MAX_SCORE=10.0
TESTS_PASSED=0
TESTS_TOTAL=5

TEMP_DIR=""

cleanup() {
    # Dọn dẹp tiến trình nền nếu còn sót
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -9 "$SERVER_PID" 2>/dev/null
    fi
    if [ -n "$C1_PID" ] && kill -0 "$C1_PID" 2>/dev/null; then
        kill -9 "$C1_PID" 2>/dev/null
    fi
    if [ -n "$C2_PID" ] && kill -0 "$C2_PID" 2>/dev/null; then
        kill -9 "$C2_PID" 2>/dev/null
    fi
    if [ -n "$TEMP_DIR" ] && [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT INT TERM

echo -e "${BLUE}${BOLD}======================================================================${NC}"
echo -e "${BLUE}${BOLD}   HỆ THỐNG ĐÁNH GIÁ TỰ ĐỘNG BÀI TẬP HW4 (IT4062 - UDP SOCKET CHAT)   ${NC}"
echo -e "${BLUE}${BOLD}======================================================================${NC}"

TARGET_INPUT="${1:-}"
TARGET_CLIENT="${2:-}"

# Hiển thị hướng dẫn
if [ "$TARGET_INPUT" == "-h" ] || [ "$TARGET_INPUT" == "--help" ]; then
    echo -e "Cú pháp sử dụng:"
    echo -e "  ${BOLD}$0${NC}                          : Tự động tìm file zip hoặc server/client để chấm"
    echo -e "  ${BOLD}$0 <file_zip>${NC}              : Giải nén, biên dịch Makefile và chấm file zip"
    echo -e "  ${BOLD}$0 <file_server> <file_client>${NC} : Chấm trực tiếp 2 file thực thi"
    exit 0
fi

# 1. Tự động nhận diện input nếu không truyền
if [ -z "$TARGET_INPUT" ]; then
    ZIP_FOUND=$(find . -maxdepth 1 -name "*_HW4.zip" ! -name "*TuCham*" ! -name "*grade*" | head -n 1)
    if [ -n "$ZIP_FOUND" ]; then
        TARGET_INPUT="$ZIP_FOUND"
    elif [ -f "./server" ] && [ -f "./client" ]; then
        TARGET_INPUT="./server"
        TARGET_CLIENT="./client"
    elif [ -f "./Makefile" ]; then
        echo -e "${CYAN}[i] Tìm thấy Makefile, đang tiến hành biên dịch 'make all'...${NC}"
        make clean all >/dev/null 2>&1
        if [ -f "./server" ] && [ -f "./client" ]; then
            TARGET_INPUT="./server"
            TARGET_CLIENT="./client"
        fi
    fi
fi

if [ -z "$TARGET_INPUT" ]; then
    echo -e "${RED}Lỗi: Không tìm thấy file zip nộp bài (*_HW4.zip) hoặc file thực thi ./server và ./client!${NC}"
    echo -e "Vui lòng chỉ định: ${BOLD}$0 <file_zip>${NC} hoặc ${BOLD}$0 ./server ./client${NC}"
    exit 1
fi

SERVER_BIN=""
CLIENT_BIN=""
WORK_DIR="."
STUDENT_NAME="SinhVien"
STUDENT_MSSV="ChuaDatTen"

# 2. Xử lý trường hợp input là file ZIP
if [[ "$TARGET_INPUT" == *.zip ]]; then
    ZIP_FILE="$(realpath "$TARGET_INPUT")"
    ZIP_BASENAME="$(basename "$ZIP_FILE")"
    echo -e "${CYAN}[*] Đang kiểm tra file nén bài nộp: ${BOLD}$ZIP_BASENAME${NC}"

    # Kiểm tra định dạng tên file zip: HotenSV_MSSV_HW4.zip
    if [[ "$ZIP_BASENAME" =~ ^([a-zA-Z0-9]+)_([0-9]+)_HW4\.zip$ ]]; then
        STUDENT_NAME="${BASH_REMATCH[1]}"
        STUDENT_MSSV="${BASH_REMATCH[2]}"
        echo -e "    ${GREEN}✔ Tên file zip đúng quy chuẩn:${NC} Họ tên = ${BOLD}$STUDENT_NAME${NC}, MSSV = ${BOLD}$STUDENT_MSSV${NC}"
    else
        echo -e "    ${YELLOW}⚠ Cảnh báo: Tên file '$ZIP_BASENAME' chưa chuẩn định dạng HotenSV_MSSV_HW4.zip (Ví dụ: NguyenVanA_20161234_HW4.zip)${NC}"
    fi

    TEMP_DIR=$(mktemp -d -t ltm_hw4_grade_XXXXXX)
    unzip -q "$ZIP_FILE" -d "$TEMP_DIR"
    WORK_DIR="$TEMP_DIR"

    # Kiểm tra nếu nén cả thư mục cha
    if [ ! -f "$WORK_DIR/Makefile" ] && [ ! -f "$WORK_DIR/makefile" ]; then
        SUBDIR=$(find "$WORK_DIR" -mindepth 1 -maxdepth 1 -type d | head -n 1)
        if [ -n "$SUBDIR" ] && ([ -f "$SUBDIR/Makefile" ] || [ -f "$SUBDIR/makefile" ]); then
            WORK_DIR="$SUBDIR"
            echo -e "    ${YELLOW}⚠ Lưu ý: File nén có chứa thư mục con $(basename "$SUBDIR"). Nên nén trực tiếp các file mã nguồn và Makefile.${NC}"
        fi
    fi

    # Kiểm tra Makefile
    if [ ! -f "$WORK_DIR/Makefile" ] && [ ! -f "$WORK_DIR/makefile" ]; then
        echo -e "${RED}    ✘ Lỗi: Không tìm thấy Makefile trong file zip!${NC}"
        echo -e "${RED}Điểm tổng kết: 0.0 / 10.0 (Không biên dịch được)${NC}"
        exit 1
    fi

    echo -e "${CYAN}[*] Đang tiến hành biên dịch mã nguồn với lệnh 'make all'...${NC}"
    COMPILE_OUTPUT=$(make -C "$WORK_DIR" clean all 2>&1)
    COMPILE_STATUS=$?

    if [ $COMPILE_STATUS -ne 0 ]; then
        echo -e "${RED}    ✘ Lỗi: Lệnh make thất bại! Chi tiết lỗi:${NC}"
        echo -e "$COMPILE_OUTPUT"
        echo -e "${RED}Điểm tổng kết: 0.0 / 10.0 (Lỗi biên dịch)${NC}"
        exit 1
    fi

    if [ ! -f "$WORK_DIR/server" ] || [ ! -f "$WORK_DIR/client" ]; then
        echo -e "${RED}    ✘ Lỗi: Make không tạo đủ 2 file thực thi 'server' và 'client'!${NC}"
        echo -e "${RED}Điểm tổng kết: 0.0 / 10.0${NC}"
        exit 1
    fi

    echo -e "    ${GREEN}✔ Makefile biên dịch thành công tạo đúng 'server' và 'client'.${NC} (+2.0đ)"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 2.0}")
    SERVER_BIN="$WORK_DIR/server"
    CLIENT_BIN="$WORK_DIR/client"
else
    # Input là các file thực thi trực tiếp
    SERVER_BIN="$(realpath "$TARGET_INPUT")"
    if [ -n "$TARGET_CLIENT" ] && [ -f "$TARGET_CLIENT" ]; then
        CLIENT_BIN="$(realpath "$TARGET_CLIENT")"
    else
        CLIENT_BIN="$(dirname "$SERVER_BIN")/client"
    fi

    if [ ! -f "$SERVER_BIN" ] || [ ! -f "$CLIENT_BIN" ]; then
        echo -e "${RED}Lỗi: Không tìm thấy cả 2 file thực thi server và client!${NC}"
        exit 1
    fi
    WORK_DIR="$(dirname "$SERVER_BIN")"
    chmod +x "$SERVER_BIN" "$CLIENT_BIN" 2>/dev/null
    echo -e "${CYAN}[*] Kiểm tra trực tiếp file thực thi: ${BOLD}$(basename "$SERVER_BIN")${NC} và ${BOLD}$(basename "$CLIENT_BIN")${NC}"
    echo -e "    ${GREEN}✔ File thực thi sẵn sàng kiểm thử.${NC} (+2.0đ)"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 2.0}")
fi

echo -e "\n${BLUE}${BOLD}--- TIẾN HÀNH CHẠY CÁC BÀI KIỂM THỬ CHỨC NĂNG ---${NC}\n"

# Hàm lấy cổng khả dụng
get_unused_port() {
    python3 -c "import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('', 0)); print(s.getsockname()[1]); s.close()"
}

# ------------------------------------------------------------------------------
# Test 1: Kiểm tra xử lý đối số dòng lệnh & Cổng (1.0 điểm)
# ------------------------------------------------------------------------------
echo -e "${BOLD}[Test 1] Kiểm tra đối số dòng lệnh và tính hợp lệ IP/Port (1.0đ)${NC}"
T1_PASS=1

# Server thieu tham so
"$SERVER_BIN" >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "  ${RED}✘ Lỗi: ./server không truyền đối số nhưng trả về mã 0!${NC}"
    T1_PASS=0
fi

# Server cong khong hop le
"$SERVER_BIN" 99999 >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "  ${RED}✘ Lỗi: ./server cổng 99999 (>65535) nhưng không báo lỗi!${NC}"
    T1_PASS=0
fi

# Client thieu tham so
"$CLIENT_BIN" >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "  ${RED}✘ Lỗi: ./client thiếu tham số nhưng trả về mã 0!${NC}"
    T1_PASS=0
fi

# Client IP khong hop le
"$CLIENT_BIN" 999.999.999.999 5500 >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "  ${RED}✘ Lỗi: ./client địa chỉ IP sai định dạng nhưng không báo lỗi!${NC}"
    T1_PASS=0
fi

if [ $T1_PASS -eq 1 ]; then
    echo -e "  ${GREEN}✔ PASSED (+1.0đ): Xử lý tham số dòng lệnh và ràng buộc IP/Port chặt chẽ.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 1.0}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Chưa xử lý tốt lỗi tham số dòng lệnh.${NC}"
fi

# ------------------------------------------------------------------------------
# Test 2: Giao tiếp giữa 2 Clients với xâu hợp lệ (2.5 điểm)
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 2] Giao tiếp giữa 2 Clients với xâu hợp lệ (chữ cái & chữ số) (2.5đ)${NC}"
TEST_PORT_2=$(get_unused_port)
SERVER_LOG="$WORK_DIR/server_test2.log"
C1_LOG="$WORK_DIR/c1_test2.log"
C2_LOG="$WORK_DIR/c2_test2.log"

"$SERVER_BIN" "$TEST_PORT_2" > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 0.3

# Khoi chay 2 clients
(
    sleep 0.2
    echo "Hello123Vietnam"
    sleep 0.4
    echo "@"
) | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_2" > "$C1_LOG" 2>&1 &
C1_PID=$!

(
    sleep 0.4
    echo "SocketUDP456"
    sleep 0.4
    echo "#"
) | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_2" > "$C2_LOG" 2>&1 &
C2_PID=$!

wait $C1_PID 2>/dev/null
wait $C2_PID 2>/dev/null
kill -INT "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

T2_PASS=1

# Client 2 phai nhan duoc chuoi "Hello123Vietnam" va IP cua Client 1
if ! grep -q "Hello123Vietnam" "$C2_LOG"; then
    echo -e "  ${RED}✘ Lỗi: Client 2 không nhận được xâu 'Hello123Vietnam' từ Client 1!${NC}"
    T2_PASS=0
fi

if ! grep -q "127.0.0.1" "$C2_LOG"; then
    echo -e "  ${RED}✘ Lỗi: Client 2 nhận xâu nhưng không có thông tin IP:Port của Client 1!${NC}"
    T2_PASS=0
fi

# Client 1 phai nhan duoc chuoi "SocketUDP456" va IP cua Client 2
if ! grep -q "SocketUDP456" "$C1_LOG"; then
    echo -e "  ${RED}✘ Lỗi: Client 1 không nhận được xâu 'SocketUDP456' từ Client 2!${NC}"
    T2_PASS=0
fi

# Server phai hien thi ca 2 xau hop le
if ! grep -q "Hello123Vietnam" "$SERVER_LOG" || ! grep -q "SocketUDP456" "$SERVER_LOG"; then
    echo -e "  ${RED}✘ Lỗi: Server không hiển thị kết quả xâu hợp lệ nhận được!${NC}"
    T2_PASS=0
fi

if [ $T2_PASS -eq 1 ]; then
    echo -e "  ${GREEN}✔ PASSED (+2.5đ): 2 Clients chuyển tiếp xâu hợp lệ thành công kèm IP:Port, Server hiển thị đầy đủ.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 2.5}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Lỗi trong quá trình giao tiếp 2 clients.${NC}"
fi

# ------------------------------------------------------------------------------
# Test 3: Kiểm tra xâu chứa ký tự không phải chữ cái hoặc chữ số (2.0 điểm)
# Yêu cầu đề bài: Gửi lại thông báo lỗi và (Không hiển thị kết quả phía server)
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 3] Kiểm tra xâu chứa ký tự không hợp lệ (Không hiển thị tại Server) (2.0đ)${NC}"
TEST_PORT_3=$(get_unused_port)
SERVER_LOG="$WORK_DIR/server_test3.log"
C1_LOG="$WORK_DIR/c1_test3.log"
C2_LOG="$WORK_DIR/c2_test3.log"

"$SERVER_BIN" "$TEST_PORT_3" > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 0.3

# Client 1 gui xau co dau cach va xau co ky tu dac biet
(
    sleep 0.2
    echo "hello world"
    sleep 0.3
    echo "hanoi!@#2026"
    sleep 0.3
    echo "@"
) | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_3" > "$C1_LOG" 2>&1 &
C1_PID=$!

(
    sleep 0.8
    echo "#"
) | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_3" > "$C2_LOG" 2>&1 &
C2_PID=$!

wait $C1_PID 2>/dev/null
wait $C2_PID 2>/dev/null
kill -INT "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

T3_PASS=1

# 1. Server TUYET DOI KHONG hien thi "hello world" hay "hanoi!@#2026"
if grep -q "hello world" "$SERVER_LOG" || grep -q "hanoi!@#2026" "$SERVER_LOG"; then
    echo -e "  ${RED}✘ Lỗi: Đề bài yêu cầu KHÔNG hiển thị kết quả phía server, nhưng server vẫn in xâu không hợp lệ!${NC}"
    T3_PASS=0
fi

# 2. Client 1 phai nhan duoc thong bao loi
if ! grep -qiE "(loi|error|invalid)" "$C1_LOG"; then
    echo -e "  ${RED}✘ Lỗi: Server không gửi thông báo lỗi về cho Client 1 khi gửi chuỗi chứa ký tự không hợp lệ!${NC}"
    T3_PASS=0
fi

# 3. Client 2 khong duoc nhan xau loi
if grep -q "hello world" "$C2_LOG" || grep -q "hanoi!@#2026" "$C2_LOG"; then
    echo -e "  ${RED}✘ Lỗi: Client 2 vẫn nhận được chuỗi chứa ký tự không hợp lệ!${NC}"
    T3_PASS=0
fi

if [ $T3_PASS -eq 1 ]; then
    echo -e "  ${GREEN}✔ PASSED (+2.0đ): Phản hồi lỗi chính xác cho Client 1, Client 2 không bị ảnh hưởng, Server tuân thủ không in kết quả.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 2.0}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Chưa đáp ứng tiêu chuẩn xử lý ký tự không hợp lệ.${NC}"
fi

# ------------------------------------------------------------------------------
# Test 4: Chức năng kết thúc chương trình khi nhập @ hoặc # (1.5 điểm)
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 4] Kết thúc chương trình khi người dùng nhập '@' hoặc '#' (1.5đ)${NC}"
TEST_PORT_4=$(get_unused_port)

"$SERVER_BIN" "$TEST_PORT_4" >/dev/null 2>&1 &
SERVER_PID=$!
sleep 0.3

T4_PASS=1

# Test @
(echo "@") | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_4" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "  ${RED}✘ Lỗi: Client nhập '@' không thoát sạch sẽ với mã 0!${NC}"
    T4_PASS=0
fi

# Test #
(echo "#") | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_4" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "  ${RED}✘ Lỗi: Client nhập '#' không thoát sạch sẽ với mã 0!${NC}"
    T4_PASS=0
fi

kill -INT "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

if [ $T4_PASS -eq 1 ]; then
    echo -e "  ${GREEN}✔ PASSED (+1.5đ): Client ngắt kết nối và thoát an toàn khi nhận '@' hoặc '#'.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 1.5}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Xử lý ký tự ngắt kết nối chưa đúng.${NC}"
fi

# ------------------------------------------------------------------------------
# Test 5: Quản lý 2 Clients & Tái kết nối sau khi Client cũ thoát (1.0 điểm)
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 5] Quản lý 2 Clients & Tái sử dụng slot khi Client thoát (1.0đ)${NC}"
TEST_PORT_5=$(get_unused_port)
C2_LOG="$WORK_DIR/c2_test5.log"

"$SERVER_BIN" "$TEST_PORT_5" >/dev/null 2>&1 &
SERVER_PID=$!
sleep 0.3

# Client 1 ket noi roi thoat ngay
(echo "@") | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_5" >/dev/null 2>&1
sleep 0.2

# Client 2 ket noi va cho nhan tin
(
    sleep 0.6
    echo "#"
) | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_5" > "$C2_LOG" 2>&1 &
C2_PID=$!
sleep 0.2

# Client 3 ket noi vao slot Client 1 vua roi va gui tin nhan
(
    sleep 0.2
    echo "Client3Message888"
    sleep 0.2
    echo "@"
) | "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT_5" >/dev/null 2>&1

wait $C2_PID 2>/dev/null
kill -INT "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

T5_PASS=1
if ! grep -q "Client3Message888" "$C2_LOG"; then
    echo -e "  ${RED}✘ Lỗi: Client 3 không thể trò chuyện với Client 2 sau khi Client 1 đã thoát!${NC}"
    T5_PASS=0
fi

if [ $T5_PASS -eq 1 ]; then
    echo -e "  ${GREEN}✔ PASSED (+1.0đ): Quản lý slot kết nối tối ưu, cho phép Client mới gia nhập mượt mà.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 1.0}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Lỗi quản lý trạng thái đa client.${NC}"
fi

# ------------------------------------------------------------------------------
# TỔNG KẾT VÀ IN BẢNG ĐIỂM
# ------------------------------------------------------------------------------
echo -e "\n${BLUE}${BOLD}======================================================================${NC}"
echo -e "${BLUE}${BOLD}                    KẾT QUẢ ĐÁNH GIÁ BÀI TẬP HW4                      ${NC}"
echo -e "${BLUE}${BOLD}======================================================================${NC}"
echo -e "  Sinh viên : ${BOLD}$STUDENT_NAME${NC}"
echo -e "  MSSV      : ${BOLD}$STUDENT_MSSV${NC}"
echo -e "  Số test đạt: ${BOLD}$TESTS_PASSED / $TESTS_TOTAL${NC}"
echo -e "----------------------------------------------------------------------"

# So sánh điểm
IS_EXCELLENT=$(awk "BEGIN {print ($TOTAL_SCORE >= 9.0) ? 1 : 0}")
IS_PASS=$(awk "BEGIN {print ($TOTAL_SCORE >= 5.0) ? 1 : 0}")

if [ $IS_EXCELLENT -eq 1 ]; then
    echo -e "  ${GREEN}${BOLD}TỔNG ĐIỂM: $TOTAL_SCORE / $MAX_SCORE - XUẤT SẮC! ĐỦ ĐIỀU KIỆN NỘP BÀI.${NC}"
elif [ $IS_PASS -eq 1 ]; then
    echo -e "  ${YELLOW}${BOLD}TỔNG ĐIỂM: $TOTAL_SCORE / $MAX_SCORE - ĐẠT YÊU CẦU (Nên khắc phục các lỗi cảnh báo).${NC}"
else
    echo -e "  ${RED}${BOLD}TỔNG ĐIỂM: $TOTAL_SCORE / $MAX_SCORE - CHƯA ĐẠT (Vui lòng kiểm tra lại mã nguồn).${NC}"
fi
echo -e "${BLUE}${BOLD}======================================================================${NC}\n"

# Xóa các file log tạm nếu có
rm -f "$WORK_DIR"/*.log 2>/dev/null

exit 0
