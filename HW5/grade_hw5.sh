#!/usr/bin/env bash
# ==============================================================================
# Script chấm điểm và đánh giá tự động bài tập HW5 (Lập trình mạng - IT4062)
# Chương trình UDP Socket Đăng nhập, Quản lý tài khoản & Mã hóa Mật khẩu SHA-256
#
# Cách sử dụng:
#   ./grade_hw5.sh                             (Tự động nhận diện bài làm/zip)
#   ./grade_hw5.sh NguyenVanA_20161234_HW5.zip (Chấm file zip nộp bài)
#   ./grade_hw5.sh ./server ./client           (Chấm trực tiếp file thực thi)
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
TESTS_TOTAL=7

TEMP_DIR=""
SERVER_PID=""
CLIENT_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -9 "$SERVER_PID" 2>/dev/null
    fi
    if [ -n "$CLIENT_PID" ] && kill -0 "$CLIENT_PID" 2>/dev/null; then
        kill -9 "$CLIENT_PID" 2>/dev/null
    fi
    if [ -n "$TEMP_DIR" ] && [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT INT TERM

echo -e "${BLUE}${BOLD}======================================================================${NC}"
echo -e "${BLUE}${BOLD}   HỆ THỐNG ĐÁNH GIÁ TỰ ĐỘNG BÀI TẬP HW5 (IT4062 - UDP AUTH & SHA256) ${NC}"
echo -e "${BLUE}${BOLD}======================================================================${NC}"

TARGET_INPUT="${1:-}"
TARGET_CLIENT="${2:-}"

if [ "$TARGET_INPUT" == "-h" ] || [ "$TARGET_INPUT" == "--help" ]; then
    echo -e "Cú pháp sử dụng:"
    echo -e "  ${BOLD}$0${NC}                          : Tự động tìm file zip hoặc server/client để chấm"
    echo -e "  ${BOLD}$0 <file_zip>${NC}              : Giải nén, biên dịch Makefile và chấm file zip"
    echo -e "  ${BOLD}$0 <file_server> <file_client>${NC} : Chấm trực tiếp 2 file thực thi"
    exit 0
fi

# Tự động nhận diện bài làm
if [ -z "$TARGET_INPUT" ]; then
    ZIP_FOUND=$(find . -maxdepth 1 -name "*_HW5.zip" ! -name "*TuCham*" ! -name "*grade*" | head -n 1)
    if [ -n "$ZIP_FOUND" ]; then
        TARGET_INPUT="$ZIP_FOUND"
    elif [ -f "./server" ] && [ -f "./client" ]; then
        TARGET_INPUT="./server"
        TARGET_CLIENT="./client"
    elif [ -f "./server_sample" ] && [ -f "./client_sample" ]; then
        TARGET_INPUT="./server_sample"
        TARGET_CLIENT="./client_sample"
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
    echo -e "${RED}Lỗi: Không tìm thấy file zip nộp bài (*_HW5.zip) hoặc file thực thi (./server, ./client hoặc ./server_sample, ./client_sample)!${NC}"
    exit 1
fi

EXEC_DIR="$(pwd)"
SERVER_BIN=""
CLIENT_BIN=""
ZIP_NAME=""

# Xử lý nếu input là file zip
if [[ "$TARGET_INPUT" == *.zip ]]; then
    ZIP_NAME=$(basename "$TARGET_INPUT")
    echo -e "${CYAN}[i] Đang kiểm tra file nén nộp bài: ${BOLD}$ZIP_NAME${NC}"
    
    # Kiểm tra quy chuẩn đặt tên HotenSV_MSSV_HW5.zip
    if [[ ! "$ZIP_NAME" =~ ^[a-zA-Z0-9]+_[0-9]+_HW5\.zip$ ]]; then
        echo -e "${YELLOW}[!] Cảnh báo: Tên file zip '${ZIP_NAME}' chưa đúng định dạng chuẩn 'HotenSV_MSSV_HW5.zip'.${NC}"
    else
        echo -e "${GREEN}[✔] Tên file zip đúng quy định: $ZIP_NAME${NC}"
    fi

    TEMP_DIR=$(mktemp -d -t hw5_grade_XXXXXX)
    unzip -q "$TARGET_INPUT" -d "$TEMP_DIR"
    
    # Tìm Makefile
    if [ -f "$TEMP_DIR/Makefile" ]; then
        EXEC_DIR="$TEMP_DIR"
    else
        INNER_DIR=$(find "$TEMP_DIR" -mindepth 1 -maxdepth 2 -type f -name "Makefile" -exec dirname {} \;)
        if [ -n "$INNER_DIR" ]; then
            EXEC_DIR="$INNER_DIR"
        else
            echo -e "${RED}[LỖI] Không tìm thấy Makefile trong file zip nộp bài!${NC}"
            exit 1
        fi
    fi
    
    echo -e "${CYAN}[i] Tiến hành biên dịch mã nguồn với Makefile trong thư mục tạm...${NC}"
    (cd "$EXEC_DIR" && make clean all > make.log 2>&1)
    
    SERVER_BIN="$EXEC_DIR/server"
    CLIENT_BIN="$EXEC_DIR/client"
else
    SERVER_BIN="$(realpath "$TARGET_INPUT")"
    if [ -n "$TARGET_CLIENT" ] && [ -f "$TARGET_CLIENT" ]; then
        CLIENT_BIN="$(realpath "$TARGET_CLIENT")"
    else
        DIRNAME_SERVER="$(dirname "$SERVER_BIN")"
        if [ -f "$DIRNAME_SERVER/client" ]; then
            CLIENT_BIN="$(realpath "$DIRNAME_SERVER/client")"
        elif [ -f "$DIRNAME_SERVER/client_sample" ]; then
            CLIENT_BIN="$(realpath "$DIRNAME_SERVER/client_sample")"
        else
            CLIENT_BIN="$DIRNAME_SERVER/client"
        fi
    fi
    EXEC_DIR="$(dirname "$SERVER_BIN")"
    chmod +x "$SERVER_BIN" "$CLIENT_BIN" 2>/dev/null
    echo -e "${CYAN}[i] Kiểm tra trực tiếp file thực thi: ${BOLD}$(basename "$SERVER_BIN")${NC} và ${BOLD}$(basename "$CLIENT_BIN")${NC}"
fi

reset_account_file() {
    cat << 'EOF' > "$EXEC_DIR/account.txt"
hust hust123 1
soict soict123 0
admin admin123 1
guest guest123 1
EOF
}

# Đảm bảo file account.txt tồn tại
if [ ! -f "$EXEC_DIR/account.txt" ]; then
    reset_account_file
fi

# ==============================================================================
# BÀI KIỂM THỬ 1: KIỂM TRA FILE THỰC THI & BIÊN DỊCH (1.5 điểm)
# ==============================================================================
echo -e "\n${BOLD}${BLUE}--- [Test 1/7] Kiểm tra File thực thi & Khả năng biên dịch (1.5đ) ---${NC}"
T1_PASS=1
if [ ! -f "$SERVER_BIN" ] || [ ! -x "$SERVER_BIN" ]; then
    echo -e "${RED}[FAIL] Không tìm thấy file thực thi '$(basename "$SERVER_BIN")' hoặc không có quyền thực thi!${NC}"
    T1_PASS=0
fi
if [ ! -f "$CLIENT_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo -e "${RED}[FAIL] Không tìm thấy file thực thi '$(basename "$CLIENT_BIN")' hoặc không có quyền thực thi!${NC}"
    T1_PASS=0
fi

if [ $T1_PASS -eq 1 ]; then
    if [ -n "$ZIP_NAME" ]; then
        echo -e "${GREEN}[PASS] Biên dịch hoàn tất tạo đúng 2 file 'server' và 'client'. (+1.5đ)${NC}"
    else
        echo -e "${GREEN}[PASS] File thực thi '$(basename "$SERVER_BIN")' và '$(basename "$CLIENT_BIN")' hợp lệ, sẵn sàng kiểm thử. (+1.5đ)${NC}"
    fi
    TOTAL_SCORE=$(echo "$TOTAL_SCORE + 1.5" | bc)
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] File thực thi không đạt yêu cầu hoặc lỗi biên dịch. (0.0/1.5đ)${NC}"
fi

# ==============================================================================
# BÀI KIỂM THỬ 2: XỬ LÝ THAM SỐ DÒNG LỆNH & CỔNG (1.0 điểm)
# ==============================================================================
echo -e "\n${BOLD}${BLUE}--- [Test 2/7] Kiểm tra xử lý tham số dòng lệnh & Cổng (1.0đ) ---${NC}"
T2_PASS=1

# Server không tham số
"$SERVER_BIN" >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${RED}[FAIL] Server không truyền tham số cổng phải thoát với mã lỗi khác 0!${NC}"
    T2_PASS=0
fi

# Server cổng > 65535
"$SERVER_BIN" 99999 >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${RED}[FAIL] Server cổng 99999 (> 65535) phải báo lỗi!${NC}"
    T2_PASS=0
fi

# Client thiếu tham số
"$CLIENT_BIN" >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${RED}[FAIL] Client không truyền đối số phải thoát với mã lỗi!${NC}"
    T2_PASS=0
fi

# Client IP sai định dạng
"$CLIENT_BIN" 999.999.999.999 5500 >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${RED}[FAIL] Client IP không hợp lệ phải thoát với mã lỗi!${NC}"
    T2_PASS=0
fi

if [ $T2_PASS -eq 1 ]; then
    echo -e "${GREEN}[PASS] Xử lý tham số dòng lệnh, địa chỉ IP và cổng chuẩn xác. (+1.0đ)${NC}"
    TOTAL_SCORE=$(echo "$TOTAL_SCORE + 1.0" | bc)
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] Kiểm tra tham số dòng lệnh chưa đạt yêu cầu. (0.0/1.0đ)${NC}"
fi

# ==============================================================================
# BÀI KIỂM THỬ 3: KỊCH BẢN XÁC THỰC TÀI KHOẢN (2.0 điểm)
# ==============================================================================
echo -e "\n${BOLD}${BLUE}--- [Test 3/7] Kịch bản Đăng nhập & Khóa tài khoản (2.0đ) ---${NC}"
reset_account_file
TEST_PORT=$((21000 + RANDOM % 5000))

# Khởi động server
(cd "$EXEC_DIR" && "$SERVER_BIN" "$TEST_PORT" >/dev/null 2>&1) &
SERVER_PID=$!
sleep 0.4

PYTHON_CHECK=$(python3 - << EOF
import socket, sys

port = $TEST_PORT
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)

def send_recv(msg):
    sock.sendto(msg.encode(), ('127.0.0.1', port))
    data, _ = sock.recvfrom(2048)
    return data.decode().strip()

try:
    # 1. Login dung
    r1 = send_recv("hust")
    if "Insert password" not in r1:
        sys.exit(1)
    r2 = send_recv("hust123")
    if r2 != "OK":
        sys.exit(2)
    send_recv("bye")

    # 2. Login sai lan 1
    send_recv("hust")
    r_err1 = send_recv("hust12")
    if r_err1 != "Not OK":
        sys.exit(3)

    # 3. Login sai lan 2 va lan 3 -> block
    send_recv("hust")
    send_recv("hust12")
    send_recv("hust")
    r_block = send_recv("Hust12")
    if r_block != "Account is blocked":
        sys.exit(4)

    # 4. Login account status = 0 (soict)
    send_recv("soict")
    r_notready = send_recv("Soict123")
    if r_notready != "Account not ready":
        sys.exit(5)

    sys.exit(0)
except Exception as e:
    sys.exit(9)
finally:
    sock.close()
EOF
)

RET=$?
kill -9 "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

# Kiểm tra xem file account.txt có cập nhật status = 0 cho hust
HUST_STATUS=$(grep "^hust " "$EXEC_DIR/account.txt" | awk '{print $3}')

if [ $RET -eq 0 ] && [ "$HUST_STATUS" == "0" ]; then
    echo -e "${GREEN}[PASS] Đăng nhập OK, Not OK, khóa tài khoản sau 3 lần sai & cập nhật file account.txt chuẩn xác. (+2.0đ)${NC}"
    TOTAL_SCORE=$(echo "$TOTAL_SCORE + 2.0" | bc)
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] Kịch bản xác thực tài khoản thất bại (Mã lỗi kiểm thử: $RET, Hust status: $HUST_STATUS). (0.0/2.0đ)${NC}"
fi

# ==============================================================================
# BÀI KIỂM THỬ 4: MÃ HÓA SHA-256 & BẮT LỖI KÝ TỰ (2.0 điểm)
# ==============================================================================
echo -e "\n${BOLD}${BLUE}--- [Test 4/7] Mã hóa Mật khẩu SHA-256 & Bắt lỗi xâu (2.0đ) ---${NC}"
reset_account_file
TEST_PORT=$((26000 + RANDOM % 5000))

(cd "$EXEC_DIR" && "$SERVER_BIN" "$TEST_PORT" >/dev/null 2>&1) &
SERVER_PID=$!
sleep 0.4

PYTHON_HASH_CHECK=$(python3 - << EOF
import socket, hashlib, sys

port = $TEST_PORT
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)

def send_recv(msg):
    sock.sendto(msg.encode(), ('127.0.0.1', port))
    data, _ = sock.recvfrom(2048)
    return data.decode().strip()

try:
    send_recv("hust")
    if send_recv("hust123") != "OK":
        sys.exit(1)

    # 1. 1a2b3cd
    h1 = hashlib.sha256("1a2b3cd".encode()).hexdigest()
    if send_recv("1a2b3cd") != h1:
        sys.exit(2)

    # 2. 123
    h2 = hashlib.sha256("123".encode()).hexdigest()
    if send_recv("123") != h2:
        sys.exit(3)

    # 3. abcd
    h3 = hashlib.sha256("abcd".encode()).hexdigest()
    if send_recv("abcd") != h3:
        sys.exit(4)

    # 4. Ab15CD$ -> Error
    if send_recv("Ab15CD$") != "Error":
        sys.exit(5)

    # 5. Chua ky tu dac biet khac -> Error
    if send_recv("test@123") != "Error":
        sys.exit(6)

    sys.exit(0)
except Exception as e:
    sys.exit(9)
finally:
    sock.close()
EOF
)

RET=$?
kill -9 "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

if [ $RET -eq 0 ]; then
    echo -e "${GREEN}[PASS] Mã hóa SHA-256 các chuỗi chữ/số và bắt lỗi ký tự đặc biệt 'Error' hoàn toàn chính xác. (+2.0đ)${NC}"
    TOTAL_SCORE=$(echo "$TOTAL_SCORE + 2.0" | bc)
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] Kiểm tra mã hóa SHA-256 hoặc bắt lỗi xâu thất bại (Mã lỗi: $RET). (0.0/2.0đ)${NC}"
fi

# ==============================================================================
# BÀI KIỂM THỬ 5: TÍCH HỢP CLIENT - SERVER THEO KỊCH BẢN (1.5 điểm)
# ==============================================================================
echo -e "\n${BOLD}${BLUE}--- [Test 5/7] Tích hợp Client - Server theo kịch bản mẫu đề bài (1.5đ) ---${NC}"
reset_account_file
TEST_PORT=$((31000 + RANDOM % 5000))

(cd "$EXEC_DIR" && "$SERVER_BIN" "$TEST_PORT" >/dev/null 2>&1) &
SERVER_PID=$!
sleep 0.4

CLIENT_INPUT="hust\nhust123\n1a2b3cd\n123\nabcd\nAb15CD$\nbye\n"
CLIENT_OUT=$(printf "%b" "$CLIENT_INPUT" | (cd "$EXEC_DIR" && "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT" 2>&1))
C_RET=$?

kill -9 "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

T5_PASS=1
if [ $C_RET -ne 0 ]; then
    T5_PASS=0
fi

# Kiểm tra các dòng output
for EXPECTED_LINE in "username:" "password:" "OK" "fdef822088214fe045b7bccaabe13faaffcb2e107f13c55bd22ba78bc1a87f20" "a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3" "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589" "Error" "Goodbye hust"; do
    if ! echo "$CLIENT_OUT" | grep -Fqi "$EXPECTED_LINE"; then
        T5_PASS=0
        echo -e "${YELLOW}[!] Thiếu dòng output kỳ vọng: '$EXPECTED_LINE'${NC}"
    fi
done

if [ $T5_PASS -eq 1 ]; then
    echo -e "${GREEN}[PASS] Giao tiếp tích hợp Client - Server hoàn toàn khớp với kịch bản đề bài. (+1.5đ)${NC}"
    TOTAL_SCORE=$(echo "$TOTAL_SCORE + 1.5" | bc)
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] Kịch bản tương tác Client - Server chưa khớp chính xác. (0.0/1.5đ)${NC}"
fi

# ==============================================================================
# BÀI KIỂM THỬ 6: THOÁT KHI NHẬP XÂU RỖNG (1.0 điểm)
# ==============================================================================
echo -e "\n${BOLD}${BLUE}--- [Test 6/7] Thoát ứng dụng khi người dùng nhập xâu rỗng (1.0đ) ---${NC}"
reset_account_file
TEST_PORT=$((36000 + RANDOM % 5000))

(cd "$EXEC_DIR" && "$SERVER_BIN" "$TEST_PORT" >/dev/null 2>&1) &
SERVER_PID=$!
sleep 0.4

CLIENT_EMPTY_INPUT="hust\nhust123\n1a2b3cd\n\n"
CLIENT_OUT=$(printf "%b" "$CLIENT_EMPTY_INPUT" | (cd "$EXEC_DIR" && timeout 4 "$CLIENT_BIN" 127.0.0.1 "$TEST_PORT" 2>&1))
C_RET=$?

kill -9 "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

if [ $C_RET -eq 0 ] && echo "$CLIENT_OUT" | grep -Fq "Goodbye hust"; then
    echo -e "${GREEN}[PASS] Người dùng nhập xâu rỗng: Client gửi bye, hiển thị 'Goodbye hust' và thoát an toàn. (+1.0đ)${NC}"
    TOTAL_SCORE=$(echo "$TOTAL_SCORE + 1.0" | bc)
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] Xử lý thoát với xâu rỗng chưa đạt yêu cầu (Returncode: $C_RET). (0.0/1.0đ)${NC}"
fi

# ==============================================================================
# BÀI KIỂM THỬ 7: PHỤC VỤ ĐỒNG THỜI NHIỀU CLIENT QUA UDP (1.0 điểm)
# ==============================================================================
echo -e "\n${BOLD}${BLUE}--- [Test 7/7] Kiểm tra phục vụ đồng thời nhiều Client cùng lúc (1.0đ) ---${NC}"
reset_account_file
TEST_PORT=$((41000 + RANDOM % 5000))

(cd "$EXEC_DIR" && "$SERVER_BIN" "$TEST_PORT" >/dev/null 2>&1) &
SERVER_PID=$!
sleep 0.4

PYTHON_CONCURRENT_CHECK=$(python3 - << EOF
import socket, sys, hashlib

port = $TEST_PORT
s1 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s1.settimeout(2.0)
s2.settimeout(2.0)

try:
    s1.sendto("hust".encode(), ('127.0.0.1', port))
    r1, _ = s1.recvfrom(2048)
    if "Insert password" not in r1.decode(): sys.exit(1)

    s2.sendto("admin".encode(), ('127.0.0.1', port))
    r2, _ = s2.recvfrom(2048)
    if "Insert password" not in r2.decode(): sys.exit(2)

    s1.sendto("hust123".encode(), ('127.0.0.1', port))
    r1, _ = s1.recvfrom(2048)
    if r1.decode().strip() != "OK": sys.exit(3)

    s2.sendto("admin123".encode(), ('127.0.0.1', port))
    r2, _ = s2.recvfrom(2048)
    if r2.decode().strip() != "OK": sys.exit(4)

    s1.sendto("1a2b3cd".encode(), ('127.0.0.1', port))
    r1, _ = s1.recvfrom(2048)
    if r1.decode().strip() != hashlib.sha256("1a2b3cd".encode()).hexdigest(): sys.exit(5)

    s2.sendto("123".encode(), ('127.0.0.1', port))
    r2, _ = s2.recvfrom(2048)
    if r2.decode().strip() != hashlib.sha256("123".encode()).hexdigest(): sys.exit(6)

    s1.sendto("bye".encode(), ('127.0.0.1', port))
    r1, _ = s1.recvfrom(2048)
    if r1.decode().strip() != "Goodbye hust": sys.exit(7)

    s2.sendto("abcd".encode(), ('127.0.0.1', port))
    r2, _ = s2.recvfrom(2048)
    if r2.decode().strip() != hashlib.sha256("abcd".encode()).hexdigest(): sys.exit(8)

    s2.sendto("bye".encode(), ('127.0.0.1', port))
    r2, _ = s2.recvfrom(2048)
    if r2.decode().strip() != "Goodbye admin": sys.exit(9)

    sys.exit(0)
except Exception:
    sys.exit(99)
finally:
    s1.close()
    s2.close()
EOF
)

RET=$?
kill -9 "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
SERVER_PID=""

if [ $RET -eq 0 ]; then
    echo -e "${GREEN}[PASS] Server phục vụ độc lập, chính xác nhiều Client cùng lúc qua UDP. (+1.0đ)${NC}"
    TOTAL_SCORE=$(echo "$TOTAL_SCORE + 1.0" | bc)
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] Kiểm tra đồng thời nhiều client thất bại (Mã lỗi: $RET). (0.0/1.0đ)${NC}"
fi

# ==============================================================================
# BẢNG TỔNG KẾT ĐIỂM
# ==============================================================================
reset_account_file
echo -e "\n${BLUE}${BOLD}======================================================================${NC}"
echo -e "${BLUE}${BOLD}                       TỔNG KẾT ĐÁNH GIÁ HW5                         ${NC}"
echo -e "${BLUE}${BOLD}======================================================================${NC}"
echo -e " Số bài kiểm thử thành công: ${BOLD}${TESTS_PASSED}/${TESTS_TOTAL}${NC}"
echo -e " Tổng điểm đánh giá        : ${BOLD}${TOTAL_SCORE} / ${MAX_SCORE}${NC}"

IS_PERFECT=$(echo "$TOTAL_SCORE >= 9.0" | bc)
if [ "$IS_PERFECT" -eq 1 ]; then
    echo -e "\n${GREEN}${BOLD}🏆 KẾT LUẬN: BÀI LÀM ĐẠT CHẤT LƯỢNG XUẤT SẮC! SẴN SÀNG NỘP BÀI.${NC}"
else
    echo -e "\n${YELLOW}${BOLD}⚠️ KẾT LUẬN: CẦN CHỈNH SỬA THÊM CÁC MỤC CHƯA ĐẠT ĐIỂM TRƯỚC KHI NỘP.${NC}"
fi
echo -e "${BLUE}${BOLD}======================================================================${NC}"

exit 0
