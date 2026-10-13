#include "md5.h"                   /* Khai báo MD5_CTX và các hàm MD5. */

#include <stdlib.h>                /* mkstemp, fdopen, popen, pclose. */
#include <string.h>                /* strlen, strcpy, memset. */
#include <sys/types.h>             /* Kiểu descriptor do mkstemp trả về. */
#include <unistd.h>                /* close, unlink. */

/*
 * Module này không tự cài đặt thuật toán MD5.
 *
 * Dữ liệu được ghi vào một tệp tạm theo từng lần md5_update(). Khi gọi
 * md5_final(), chương trình chạy lệnh Linux:
 *
 *     md5sum <temporary-file>
 *
 * thông qua popen(). Cách này cho phép server/client dùng md5sum của Linux
 * nhưng vẫn giữ API md5_init/update/final quen thuộc trong phần còn lại.
 */

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

void md5_init(MD5_CTX *context)
{
    int file_descriptor;             /* Descriptor tệp trả về từ mkstemp. */

    memset(context, 0, sizeof(*context)); /* Đưa trạng thái về giá trị ban đầu. */
    strcpy(context->temporary_path, "/tmp/hw6-md5-XXXXXX");
    /*
     * mkstemp(): thay XXXX bằng chuỗi ngẫu nhiên và mở tệp tạm an toàn.
     * Trả descriptor >= 0 nếu thành công, -1 nếu không tạo được tệp.
     */
    file_descriptor = mkstemp(context->temporary_path);
    if (file_descriptor < 0) {
        return;
    }
    /*
     * fdopen(): chuyển descriptor thành FILE* để md5_update() dùng fwrite.
     * Nếu fdopen lỗi, phải đóng descriptor và xóa tệp tạm.
     */
    context->temporary_file = fdopen(file_descriptor, "wb");
    if (context->temporary_file == NULL) {
        close(file_descriptor);
        unlink(context->temporary_path);
        context->temporary_path[0] = '\0';
    }
}

void md5_update(MD5_CTX *context, const void *data, size_t length)
{
    if (context->temporary_file == NULL || length == 0) {
        return;
    }
    /*
     * fwrite(): ghi dữ liệu vào tệp tạm. Việc ghi theo từng chunk giúp
     * xử lý tệp lớn mà không cần giữ toàn bộ nội dung trong bộ nhớ.
     */
    if (fwrite(data, 1, length, context->temporary_file) != length) {
        /*
         * Không thể trả mã lỗi qua API update hiện tại; đánh dấu phiên lỗi
         * bằng cách đóng FILE*. md5_final() sẽ trả digest toàn số 0.
         */
        fclose(context->temporary_file);
        context->temporary_file = NULL;
    }
}

void md5_final(unsigned char digest[MD5_DIGEST_LENGTH], MD5_CTX *context)
{
    char command[128];              /* Lệnh md5sum truyền cho shell. */
    char result[128];               /* Dòng kết quả: 32 hex + tên tệp. */
    FILE *pipe;                     /* Pipe đọc stdout của md5sum. */
    size_t i;                       /* Chỉ số byte digest. */

    memset(digest, 0, MD5_DIGEST_LENGTH);
    if (context->temporary_file == NULL || context->temporary_path[0] == '\0') {
        return;
    }
    /*
     * fflush(): đẩy dữ liệu còn nằm trong bộ đệm stdio xuống tệp.
     * fclose(): đóng FILE* trước khi tiến trình md5sum đọc tệp.
     */
    fflush(context->temporary_file);
    fclose(context->temporary_file);
    context->temporary_file = NULL;

    /*
     * Tên tệp do mkstemp sinh ra chỉ nằm trong /tmp và không chứa dấu nháy,
     * nên có thể đưa an toàn vào lệnh md5sum đã bao bởi dấu nháy đơn.
     */
    snprintf(command, sizeof(command), "md5sum '%s'", context->temporary_path);
    /*
     * popen(): tạo pipe và chạy lệnh shell.
     *   - command: lệnh Linux cần chạy.
     *   - "r": mở stdout của lệnh để chương trình đọc.
     *   - Trả FILE* nếu thành công, NULL nếu lỗi.
     */
    pipe = popen(command, "r");
    if (pipe != NULL && fgets(result, sizeof(result), pipe) != NULL) {
        for (i = 0; i < MD5_DIGEST_LENGTH; ++i) {
            int high = hex_value(result[i * 2]);       /* Nửa byte cao. */
            int low = hex_value(result[i * 2 + 1]);    /* Nửa byte thấp. */
            if (high < 0 || low < 0) {
                memset(digest, 0, MD5_DIGEST_LENGTH);
                break;
            }
            digest[i] = (unsigned char)((high << 4) | low);
        }
    }
    if (pipe != NULL) {
        /*
         * pclose(): đóng pipe và chờ tiến trình md5sum kết thúc.
         * Có thể dùng giá trị trả về để kiểm tra lệnh có thất bại hay không.
         */
        pclose(pipe);
    }
    /*
     * unlink(): xóa pathname tệp tạm sau khi md5sum đã đọc xong.
     * Tệp tạm không được để lại vì chỉ phục vụ một phiên tính MD5.
     */
    unlink(context->temporary_path);
}

void md5_hex(const unsigned char digest[MD5_DIGEST_LENGTH],
             char output[MD5_DIGEST_LENGTH * 2 + 1])
{
    static const char hex[] = "0123456789ABCDEF"; /* Bảng mã hex in hoa. */
    size_t i;                                     /* Chỉ số byte digest. */

    for (i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        output[i * 2] = hex[digest[i] >> 4];
        output[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    output[MD5_DIGEST_LENGTH * 2] = '\0';
}

void md5_string(const char *text, char output[MD5_DIGEST_LENGTH * 2 + 1])
{
    MD5_CTX context;                  /* Phiên tính MD5 của xâu. */
    unsigned char digest[MD5_DIGEST_LENGTH]; /* Kết quả nhị phân. */

    md5_init(&context);
    md5_update(&context, text, strlen(text));
    md5_final(digest, &context);
    md5_hex(digest, output);
}
