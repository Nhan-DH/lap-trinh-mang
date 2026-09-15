#!/usr/bin/env bash
# ==============================================================================
# Script chấm điểm và đánh giá tự động bài tập HW2 (Lập trình mạng - IT4062)
# Dành cho sinh viên tự kiểm thử và giảng viên chấm bài
#
# Cách sử dụng:
#   ./grade_hw2.sh [đường_dẫn_file_zip_hoặc_file_resolver]
# Ví dụ:
#   ./grade_hw2.sh                          (Tự động nhận diện bài làm trong thư mục)
#   ./grade_hw2.sh ./resolver               (Chấm trực tiếp file thực thi)
#   ./grade_hw2.sh NguyenVanA_20261234_HW2.zip (Chấm file zip nộp bài)
#   ./grade_hw2.sh ./resolver_sample        (Chạy thử nghiệm file mẫu chuẩn)
# ==============================================================================

# Thiết lập màu sắc hiển thị terminal
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
TESTS_TOTAL=6

TEMP_DIR=""

cleanup() {
    if [ -n "$TEMP_DIR" ] && [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT

echo -e "${BLUE}${BOLD}======================================================================${NC}"
echo -e "${BLUE}${BOLD}      HỆ THỐNG ĐÁNH GIÁ TỰ ĐỘNG BÀI TẬP HW2 (IT4062 - RESOLVER)       ${NC}"
echo -e "${BLUE}${BOLD}======================================================================${NC}"

TARGET_INPUT="${1:-}"

# Hiển thị trợ giúp nếu truyền cờ -h hoặc --help
if [ "$TARGET_INPUT" == "-h" ] || [ "$TARGET_INPUT" == "--help" ]; then
    echo -e "Cú pháp sử dụng:"
    echo -e "  ${BOLD}$0${NC}                          : Tự động tìm file zip hoặc resolver để chấm"
    echo -e "  ${BOLD}$0 <file_thuc_thi>${NC}          : Chấm trực tiếp file thực thi (ví dụ: ./resolver)"
    echo -e "  ${BOLD}$0 <file_zip>${NC}              : Giải nén, biên dịch Makefile và chấm file zip"
    echo -e "  ${BOLD}$0 --demo${NC}                  : Chạy kiểm thử trên file mẫu đi kèm (resolver_sample)"
    exit 0
fi

# Nếu truyền cờ --demo
if [ "$TARGET_INPUT" == "--demo" ]; then
    if [ -f "./resolver_sample" ]; then
        TARGET_INPUT="./resolver_sample"
    elif [ -f "./resolver" ]; then
        TARGET_INPUT="./resolver"
    fi
fi

# 1. Tự động nhận diện input nếu không truyền đối số
if [ -z "$TARGET_INPUT" ]; then
    # Tìm file zip của sinh viên (loại trừ các file zip công cụ/mẫu)
    ZIP_FOUND=$(find . -maxdepth 1 -name "*_HW2.zip" ! -name "*TuCham*" ! -name "*grade*" | head -n 1)
    if [ -n "$ZIP_FOUND" ]; then
        TARGET_INPUT="$ZIP_FOUND"
    elif [ -f "./resolver" ]; then
        TARGET_INPUT="./resolver"
    elif [ -f "./resolver_sample" ]; then
        echo -e "${CYAN}[i] Không thấy bài làm của sinh viên, tự động chạy với file mẫu: ${BOLD}./resolver_sample${NC}"
        TARGET_INPUT="./resolver_sample"
    elif [ -f "./Makefile" ]; then
        echo -e "${CYAN}[i] Tìm thấy Makefile, đang tiến hành biên dịch 'make' để tạo ./resolver...${NC}"
        make clean all >/dev/null 2>&1
        if [ -f "./resolver" ]; then
            TARGET_INPUT="./resolver"
        fi
    fi
fi

if [ -z "$TARGET_INPUT" ]; then
    echo -e "${RED}Lỗi: Không tìm thấy file zip nộp bài (*_HW2.zip) hoặc file thực thi ./resolver!${NC}"
    echo -e "Vui lòng chỉ định đường dẫn: ${BOLD}$0 <file_zip | ./resolver>${NC}"
    exit 1
fi

RESOLVER_BIN=""
WORK_DIR="."
STUDENT_NAME="SinhVien"
STUDENT_MSSV="ChuaDatTen"

# 2. Xử lý trường hợp input là file ZIP
if [[ "$TARGET_INPUT" == *.zip ]]; then
    ZIP_FILE="$(realpath "$TARGET_INPUT")"
    ZIP_BASENAME="$(basename "$ZIP_FILE")"
    echo -e "${CYAN}[*] Đang kiểm tra file nén bài nộp: ${BOLD}$ZIP_BASENAME${NC}"

    # Kiểm tra định dạng tên file zip: HotenSV_MSSV_HW2.zip
    if [[ "$ZIP_BASENAME" =~ ^([a-zA-Z0-9]+)_([0-9]+)_HW2\.zip$ ]]; then
        STUDENT_NAME="${BASH_REMATCH[1]}"
        STUDENT_MSSV="${BASH_REMATCH[2]}"
        echo -e "    ${GREEN}✔ Tên file zip đúng quy chuẩn:${NC} Họ tên = ${BOLD}$STUDENT_NAME${NC}, MSSV = ${BOLD}$STUDENT_MSSV${NC}"
    else
        echo -e "    ${YELLOW}⚠ Cảnh báo: Tên file '$ZIP_BASENAME' chưa đúng định dạng HotenSV_MSSV_HW2.zip (Ví dụ: NguyenVanA_20261234_HW2.zip)${NC}"
    fi

    # Tạo thư mục tạm để giải nén
    TEMP_DIR=$(mktemp -d -t ltm_hw2_grade_XXXXXX)
    unzip -q "$ZIP_FILE" -d "$TEMP_DIR"
    WORK_DIR="$TEMP_DIR"

    # Kiểm tra nếu sinh viên nén cả thư mục cha
    if [ ! -f "$WORK_DIR/Makefile" ] && [ ! -f "$WORK_DIR/makefile" ]; then
        SUBDIR=$(find "$WORK_DIR" -mindepth 1 -maxdepth 1 -type d | head -n 1)
        if [ -n "$SUBDIR" ] && ([ -f "$SUBDIR/Makefile" ] || [ -f "$SUBDIR/makefile" ]); then
            WORK_DIR="$SUBDIR"
            echo -e "    ${YELLOW}⚠ Lưu ý: File nén có chứa thư mục con $(basename "$SUBDIR"). Nên nén trực tiếp các file mã nguồn và Makefile.${NC}"
        fi
    fi

    # Kiểm tra sự tồn tại của Makefile
    if [ ! -f "$WORK_DIR/Makefile" ] && [ ! -f "$WORK_DIR/makefile" ]; then
        echo -e "${RED}    ✘ Lỗi: Không tìm thấy Makefile trong file zip!${NC}"
        echo -e "${RED}Điểm tổng kết: 0.0 / 10.0 (Không biên dịch được)${NC}"
        exit 1
    fi

    echo -e "${CYAN}[*] Đang tiến hành biên dịch mã nguồn với lệnh 'make'...${NC}"
    COMPILE_OUTPUT=$(make -C "$WORK_DIR" clean all 2>&1)
    COMPILE_STATUS=$?

    if [ $COMPILE_STATUS -ne 0 ]; then
        echo -e "${RED}    ✘ Lỗi: Lệnh make thất bại! Chi tiết lỗi:${NC}"
        echo -e "$COMPILE_OUTPUT"
        echo -e "${RED}Điểm tổng kết: 0.0 / 10.0 (Lỗi biên dịch)${NC}"
        exit 1
    fi

    if [ ! -f "$WORK_DIR/resolver" ]; then
        echo -e "${RED}    ✘ Lỗi: Lệnh make thành công nhưng không tạo ra file thực thi đúng tên 'resolver'!${NC}"
        echo -e "${RED}Điểm tổng kết: 0.0 / 10.0${NC}"
        exit 1
    fi

    echo -e "    ${GREEN}✔ Makefile biên dịch thành công tạo file thực thi 'resolver'.${NC} (+2.0đ)"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 2.0}")
    RESOLVER_BIN="$WORK_DIR/resolver"
else
    # Input là file thực thi trực tiếp
    if [ ! -f "$TARGET_INPUT" ]; then
        echo -e "${RED}Lỗi: File '$TARGET_INPUT' không tồn tại!${NC}"
        exit 1
    fi
    RESOLVER_BIN="$(realpath "$TARGET_INPUT")"
    WORK_DIR="$(dirname "$RESOLVER_BIN")"
    chmod +x "$RESOLVER_BIN" 2>/dev/null
    echo -e "${CYAN}[*] Kiểm tra trực tiếp file thực thi: ${BOLD}$(basename "$RESOLVER_BIN")${NC}"
    echo -e "    ${GREEN}✔ File thực thi sẵn sàng kiểm thử.${NC} (+2.0đ)"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 2.0}")
fi

echo -e "\n${BLUE}${BOLD}--- TIẾN HÀNH CHẠY BỘ KIỂM THỬ CHỨC NĂNG ---${NC}\n"

# ------------------------------------------------------------------------------
# Test 1: Phân giải tên miền hợp lệ bình thường (google.com) - 2.0 điểm
# ------------------------------------------------------------------------------
echo -e "${BOLD}[Test 1] Phân giải tên miền an toàn: google.com (2.0đ)${NC}"
OUTPUT_1=$("$RESOLVER_BIN" google.com 2>&1)
EXIT_CODE_1=$?

T1_PASS=1
if [ $EXIT_CODE_1 -ne 0 ]; then
    echo -e "  ${RED}✘ Lỗi: Chương trình trả về mã lỗi thoát ($EXIT_CODE_1)${NC}"
    T1_PASS=0
fi

if ! echo "$OUTPUT_1" | grep -q "Official IP:"; then
    echo -e "  ${RED}✘ Lỗi: Output thiếu nhãn 'Official IP:'${NC}"
    T1_PASS=0
fi

if echo "$OUTPUT_1" | grep -q "Alias IP:"; then
    ALIAS_LINES=$(echo "$OUTPUT_1" | sed -n '/Alias IP:/,$p' | grep -v "Alias IP:" | grep -v "This site" | grep -E '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+')
    if [ -z "$ALIAS_LINES" ]; then
        echo -e "  ${RED}✘ Lỗi: Xuất hiện nhãn 'Alias IP:' nhưng không có IP phụ nào theo sau!${NC}"
        T1_PASS=0
    fi
fi

if echo "$OUTPUT_1" | grep -q "This site is not for you!"; then
    echo -e "  ${RED}✘ Lỗi: google.com là trang an toàn nhưng bị chặn nhầm ('This site is not for you!')${NC}"
    T1_PASS=0
fi

if echo "$OUTPUT_1" | grep -q "Not found information"; then
    echo -e "  ${RED}✘ Lỗi: google.com là tên miền tồn tại nhưng lại báo 'Not found information'${NC}"
    T1_PASS=0
fi

if [ $T1_PASS -eq 1 ]; then
    OFFICIAL_IP_1=$(echo "$OUTPUT_1" | grep "Official IP:" | head -n 1 | awk '{print $NF}')
    echo -e "  ${GREEN}✔ PASSED (+2.0đ): Phân giải đúng Official IP ($OFFICIAL_IP_1), xử lý Alias IP chuẩn.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 2.0}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Không đạt yêu cầu phân giải google.com.${NC}"
    echo -e "  Output nhận được:\n$OUTPUT_1"
fi

# ------------------------------------------------------------------------------
# Test 2: Phân giải tên miền an toàn khác: cloudflare.com (1.5 điểm)
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 2] Phân giải tên miền an toàn khác: cloudflare.com (1.5đ)${NC}"
OUTPUT_2=$("$RESOLVER_BIN" cloudflare.com 2>&1)
EXIT_CODE_2=$?

T2_PASS=1
if [ $EXIT_CODE_2 -ne 0 ] || ! echo "$OUTPUT_2" | grep -q "Official IP:"; then
    T2_PASS=0
fi

if echo "$OUTPUT_2" | grep -q "This site is not for you!"; then
    echo -e "  ${RED}✘ Lỗi: cloudflare.com bị chặn nhầm!${NC}"
    T2_PASS=0
fi

if [ $T2_PASS -eq 1 ]; then
    OFFICIAL_IP_2=$(echo "$OUTPUT_2" | grep "Official IP:" | head -n 1 | awk '{print $NF}')
    echo -e "  ${GREEN}✔ PASSED (+1.5đ): Phân giải đúng cloudflare.com ($OFFICIAL_IP_2), xác nhận an toàn.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 1.5}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Lỗi phân giải tên miền cloudflare.com.${NC}"
fi

# ------------------------------------------------------------------------------
# Test 3: Tên miền không tồn tại: aznsc.test.com (1.5 điểm)
# Yêu cầu: Báo đúng chuỗi 'Not found information'
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 3] Tên miền không tồn tại: aznsc.test.com (1.5đ)${NC}"
OUTPUT_3=$("$RESOLVER_BIN" aznsc.test.com 2>&1)

if echo "$OUTPUT_3" | grep -q "Not found information"; then
    echo -e "  ${GREEN}✔ PASSED (+1.5đ): In chính xác thông báo 'Not found information'.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 1.5}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Phải in chính xác chuỗi 'Not found information'.${NC}"
    echo -e "  Output thực tế:\n$OUTPUT_3"
fi

# ------------------------------------------------------------------------------
# Test 4: Tên miền độc hại / người lớn: pornhub.com (1.5 điểm)
# Yêu cầu:
#   - Có 'Official IP:'
#   - Có 'This site is not for you!'
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 4] Kiểm tra bộ lọc Cloudflare với tên miền độc hại: pornhub.com (1.5đ)${NC}"
OUTPUT_4=$("$RESOLVER_BIN" pornhub.com 2>&1)

T4_PASS=1
if ! echo "$OUTPUT_4" | grep -q "Official IP:"; then
    echo -e "  ${RED}✘ Lỗi: Vẫn cần phân giải và in Official IP của tên miền.${NC}"
    T4_PASS=0
fi

if ! echo "$OUTPUT_4" | grep -q "This site is not for you!"; then
    echo -e "  ${RED}✘ Lỗi: Không phát hiện tên miền bị chặn bởi Cloudflare 1.1.1.3 (thiếu thông báo 'This site is not for you!')${NC}"
    T4_PASS=0
fi

if [ $T4_PASS -eq 1 ]; then
    echo -e "  ${GREEN}✔ PASSED (+1.5đ): Phát hiện thành công và in thông báo 'This site is not for you!'.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 1.5}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Không đạt yêu cầu lọc với pornhub.com.${NC}"
    echo -e "  Output nhận được:\n$OUTPUT_4"
fi

# ------------------------------------------------------------------------------
# Test 5: Tên miền người lớn kiểm tra chéo: xvideos.com (1.5 điểm)
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 5] Kiểm tra chéo bộ lọc Cloudflare với: xvideos.com (1.5đ)${NC}"
OUTPUT_5=$("$RESOLVER_BIN" xvideos.com 2>&1)

if echo "$OUTPUT_5" | grep -q "This site is not for you!"; then
    echo -e "  ${GREEN}✔ PASSED (+1.5đ): Phát hiện chính xác xvideos.com và in 'This site is not for you!'.${NC}"
    TOTAL_SCORE=$(awk "BEGIN {print $TOTAL_SCORE + 1.5}")
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${RED}✘ FAILED: Không phát hiện xvideos.com bị chặn bởi Cloudflare.${NC}"
    echo -e "  Output nhận được:\n$OUTPUT_5"
fi

# ------------------------------------------------------------------------------
# Test 6: Kiểm tra xử lý đối số dòng lệnh khi thiếu tham số
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}[Test 6] Kiểm tra xử lý đối số dòng lệnh khi thiếu tham số${NC}"
OUTPUT_6=$("$RESOLVER_BIN" 2>&1)
EXIT_CODE_6=$?

if [ $EXIT_CODE_6 -ne 0 ]; then
    echo -e "  ${GREEN}✔ PASSED: Đã xử lý bắt lỗi và thoát mã lỗi khác 0 khi thiếu tham số.${NC}"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "  ${YELLOW}⚠ Gợi ý: Nên trả về mã lỗi khác 0 (exit 1) khi người dùng không truyền tham số.${NC}"
fi

# ------------------------------------------------------------------------------
# BẢNG TỔNG KẾT VÀ ĐÁNH GIÁ ĐIỂM SỐ
# ------------------------------------------------------------------------------
echo -e "\n${BLUE}${BOLD}======================================================================${NC}"
echo -e "${BLUE}${BOLD}                       BÁO CÁO KẾT QUẢ ĐÁNH GIÁ                       ${NC}"
echo -e "${BLUE}${BOLD}======================================================================${NC}"
echo -e " Đối tượng kiểm tra: ${BOLD}$TARGET_INPUT${NC}"
echo -e " Thông tin sinh viên: ${BOLD}$STUDENT_NAME${NC} (MSSV: ${BOLD}$STUDENT_MSSV${NC})"
echo -e " Số ca kiểm thử đạt:  ${BOLD}$TESTS_PASSED / $TESTS_TOTAL${NC}"
echo -e " ----------------------------------------------------------------------"
if (( $(echo "$TOTAL_SCORE >= 9.0" | bc -l) )); then
    echo -e " TỔNG ĐIỂM: ${GREEN}${BOLD}$TOTAL_SCORE / 10.0 (XUẤT SẮC - ĐẠT YÊU CẦU NỘP BÀI)${NC}"
elif (( $(echo "$TOTAL_SCORE >= 7.0" | bc -l) )); then
    echo -e " TỔNG ĐIỂM: ${GREEN}${BOLD}$TOTAL_SCORE / 10.0 (ĐẠT YÊU CẦU)${NC}"
else
    echo -e " TỔNG ĐIỂM: ${RED}${BOLD}$TOTAL_SCORE / 10.0 (CHƯA ĐẠT - VUI LÒNG KIỂM TRA LẠI MÃ NGUỒN)${NC}"
fi
echo -e "${BLUE}${BOLD}======================================================================${NC}\n"

if (( $(echo "$TOTAL_SCORE >= 7.0" | bc -l) )); then
    exit 0
else
    exit 1
fi
