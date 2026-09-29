/*
 * server.c - Server trao đổi dữ liệu với tối đa hai client bằng UDP.
 * UDP không tạo kết nối vật lý; server nhận endpoint từ recvfrom() và dùng
 * sendto() để trả lời hoặc chuyển tiếp dữ liệu cho client còn lại.
 */

#include "protocol.h"
#include <arpa/inet.h>      /* inet_ntop(), htons(), ntohs() */
#include <errno.h>          /* errno, EINTR */
#include <netinet/in.h>     /* sockaddr_in, INADDR_ANY */
#include <stdio.h>          /* printf(), fprintf(), perror() */
#include <stdlib.h>         /* strtol(), EXIT_FAILURE, EXIT_SUCCESS */
#include <string.h>         /* memset(), strlen() */
#include <sys/socket.h>     /* socket(), bind(), recvfrom(), sendto() */
#include <unistd.h>         /* close() */

typedef struct {
	int active;
	struct sockaddr_in address;
} ClientSlot;

/**
 * @brief Hàm chính thực thi UDP Echo Server.
 * 
 * Chi tiết luồng xử lý:
 *   - Bước 1: Tạo socket UDP với domain AF_INET và type SOCK_DGRAM.
 *   - Bước 2: Thiết lập cấu trúc sockaddr_in của server (IP INADDR_ANY, PORT 5550) và gọi bind().
 *   - Bước 3: Vào vòng lặp vô hạn để nhận gói tin từ client qua recvfrom(), in thông tin ra
 *             màn hình và phản hồi lại dữ liệu cho client qua sendto().
 * 
 * @param Không có tham số đầu vào (void).
 * @return int:
 *   - Trả về 0 khi chương trình kết thúc thành công.
 *   - Thoát với exit(0) nếu xảy ra lỗi trong quá trình khởi tạo socket hoặc bind.
 */
int main(void)
{
	int server_sock;                  /* File descriptor của socket server */
	char buff[BUFF_SIZE];             /* Bộ đệm lưu trữ dữ liệu nhận và gửi */
	int bytes_sent, bytes_received;   /* Số lượng byte thực tế đã gửi hoặc nhận */
	struct sockaddr_in server;        /* Cấu trúc lưu thông tin địa chỉ của server */
	struct sockaddr_in client;        /* Cấu trúc lưu thông tin địa chỉ của client gửi đến */
	socklen_t sin_size;               /* Kích thước của cấu trúc sockaddr_in */

	/* =========================================================================
	 * Step 1: Khởi tạo socket UDP
	 * =========================================================================
	 * Hàm socket():
	 *   - Vào: 
	 *       + AF_INET: Giao thức IPv4.
	 *       + SOCK_DGRAM: Giao thức UDP (hướng datagram, không kết nối).
	 *       + 0: Giao thức mặc định tương ứng với SOCK_DGRAM (IPPROTO_UDP).
	 *   - Ra: File descriptor của socket (số nguyên không âm nếu thành công, -1 nếu lỗi).
	 */
	if ((server_sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("\nError: ");
		exit(0);
	}
	
	/* =========================================================================
	 * Step 2: Gán (bind) địa chỉ IP và Port cho socket
	 * =========================================================================
	 * Thiết lập thông tin server:
	 *   - sin_family = AF_INET (IPv4)
	 *   - sin_port = htons(PORT): Chuyển port từ Host Byte Order sang Network Byte Order.
	 *   - sin_addr.s_addr = INADDR_ANY: Lắng nghe trên tất cả các card mạng của máy.
	 *   - bzero: Xóa phần đệm sin_zero của struct sockaddr_in về 0.
	 */
	server.sin_family = AF_INET;         
	server.sin_port = htons(PORT);
	server.sin_addr.s_addr = INADDR_ANY;
	bzero(&(server.sin_zero), 8);

	/*
	 * Hàm bind():
	 *   - Vào:
	 *       + server_sock: Socket descriptor cần gắn địa chỉ.
	 *       + (struct sockaddr*)&server: Con trỏ trỏ tới cấu trúc địa chỉ server.
	 *       + sizeof(struct sockaddr): Kích thước của cấu trúc địa chỉ.
	 *   - Ra: 0 nếu thành công, -1 nếu thất bại (ví dụ: port đã bị chiếm dụng).
	 */
	if (bind(server_sock, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1) {
		perror("\nError: ");
		exit(0);
	}     
	
	/* =========================================================================
	 * Step 3: Giao tiếp và trao đổi dữ liệu với các Client
	 * =========================================================================
	 */
	printf("[SERVER] UDP Server is running and listening at port %d...\n", PORT);

	while (1) {
		sin_size = sizeof(struct sockaddr_in);
    		
		/*
		 * Hàm recvfrom(): Chờ và nhận dữ liệu từ một client bất kỳ gửi tới.
		 *   - Vào:
		 *       + server_sock: Socket descriptor nhận dữ liệu.
		 *       + buff: Con trỏ vùng đệm lưu dữ liệu nhận được.
		 *       + BUFF_SIZE - 1: Kích thước tối đa có thể đọc (chừa 1 byte cho '\0').
		 *       + 0: Cờ điều khiển (flags), 0 là chế độ nhận dữ liệu mặc định.
		 *       + (struct sockaddr*)&client: Con trỏ lưu thông tin IP/port của client gửi tới.
		 *       + &sin_size: Con trỏ chứa kích thước ban đầu của struct client.
		 *   - Ra: 
		 *       + Số bytes nhận được thực tế nếu thành công.
		 *       + -1 nếu có lỗi xảy ra.
		 */
		bytes_received = recvfrom(server_sock, buff, BUFF_SIZE - 1, 0, (struct sockaddr *)&client, &sin_size);
		
		if (bytes_received < 0) {
			perror("\nError: ");
		} else {
			buff[bytes_received] = '\0'; /* Thêm ký tự kết thúc chuỗi an toàn */
			/* In ra địa chỉ IP (inet_ntoa), Port (ntohs) của client và nội dung nhận được */
			printf("[%s:%d]: %s", inet_ntoa(client.sin_addr), ntohs(client.sin_port), buff);
		}
		
		/*
		 * Hàm sendto(): Gửi dữ liệu (echo) phản hồi lại chính client vừa gửi.
		 *   - Vào:
		 *       + server_sock: Socket descriptor dùng để gửi dữ liệu.
		 *       + buff: Vùng nhớ chứa dữ liệu cần gửi đi.
		 *       + bytes_received: Số lượng byte cần gửi (bằng đúng số byte đã nhận).
		 *       + 0: Cờ điều khiển (flags).
		 *       + (struct sockaddr*)&client: Địa chỉ đích của client cần gửi đến.
		 *       + sin_size: Kích thước của struct client.
		 *   - Ra:
		 *       + Số byte gửi thành công.
		 *       + -1 nếu có lỗi xảy ra.
		 */
		bytes_sent = sendto(server_sock, buff, bytes_received, 0, (struct sockaddr *)&client, sin_size);
		if (bytes_sent < 0) {
			perror("\nError: ");					
		}
	}
	
	/* Đóng socket khi hoàn tất */
	close(server_sock);
	return 0;
}
