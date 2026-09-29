/*
 * UDP Echo Client
 * 
 * Mô tả:
 *   Chương trình UDP Client gửi một chuỗi văn bản do người dùng nhập từ bàn phím
 *   đến UDP Server (địa chỉ 127.0.0.1, cổng 5550), sau đó chờ nhận và hiển thị
 *   thông điệp phản hồi (echo) từ server.
 */

#include <stdio.h>          /* Thư viện vào/ra chuẩn: printf, perror, fgets */
#include <stdlib.h>         /* Thư viện chuẩn: exit, malloc, free */
#include <sys/types.h>      /* Các kiểu dữ liệu hệ thống */
#include <sys/socket.h>     /* Các hàm và cấu trúc socket: socket, sendto, recvfrom */
#include <netinet/in.h>     /* Các cấu trúc địa chỉ Internet: sockaddr_in, in_addr */
#include <arpa/inet.h>      /* Các hàm chuyển đổi địa chỉ: htons, inet_addr */
#include <string.h>         /* Thao tác chuỗi và bộ nhớ: memset, bzero, strlen */
#include <unistd.h>         /* Các lời gọi hệ thống chuẩn: close */

#define SERV_PORT 5550      /* Cổng của UDP Server cần kết nối */
#define SERV_IP "127.0.0.1" /* Địa chỉ IP của Server (localhost) */
#define BUFF_SIZE 1024      /* Kích thước bộ đệm chứa dữ liệu */

/**
 * @brief Hàm chính thực thi UDP Echo Client.
 * 
 * Chi tiết luồng xử lý:
 *   - Bước 1: Tạo socket UDP phía client với domain AF_INET và type SOCK_DGRAM.
 *   - Bước 2: Thiết lập thông tin địa chỉ server đích (IP SERV_IP, Port SERV_PORT).
 *   - Bước 3: Đọc chuỗi nhập từ bàn phím, gửi dữ liệu tới server qua sendto(),
 *             chờ nhận phản hồi qua recvfrom(), in kết quả và đóng socket.
 * 
 * @param Không có tham số đầu vào (void).
 * @return int:
 *   - Trả về 0 khi chương trình kết thúc thành công hoặc sau khi hoàn tất phiên gửi/nhận.
 *   - Thoát với exit(0) nếu xảy ra lỗi trong quá trình khởi tạo socket.
 */
int main(void)
{
	int client_sock;                 /* File descriptor của socket client */
	char buff[BUFF_SIZE];            /* Bộ đệm chứa thông điệp gửi và nhận */
	struct sockaddr_in server_addr;  /* Cấu trúc lưu thông tin địa chỉ server đích */
	int bytes_sent, bytes_received;  /* Số byte thực tế đã gửi hoặc nhận */
	socklen_t sin_size;              /* Kích thước của struct sockaddr_in */
	
	/* =========================================================================
	 * Step 1: Khởi tạo socket UDP
	 * =========================================================================
	 * Hàm socket():
	 *   - Vào:
	 *       + AF_INET: Giao thức mạng IPv4.
	 *       + SOCK_DGRAM: Giao thức truyền gói tin UDP không kết nối.
	 *       + 0: Giao thức mặc định của SOCK_DGRAM (IPPROTO_UDP).
	 *   - Ra: File descriptor của socket nếu thành công, hoặc < 0 nếu thất bại.
	 */
	if ((client_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("\nError: ");
		exit(0);
	}

	/* =========================================================================
	 * Step 2: Xác định thông tin địa chỉ của Server đích
	 * =========================================================================
	 * Cấu hình struct server_addr:
	 *   - bzero: Xóa sạch toàn bộ cấu trúc về 0 để tránh dữ liệu rác.
	 *   - sin_family = AF_INET (IPv4).
	 *   - sin_port = htons(SERV_PORT): Chuyển port từ định dạng máy chủ sang mạng.
	 *   - sin_addr.s_addr = inet_addr(SERV_IP): Chuyển chuỗi IP "127.0.0.1" sang số nguyên 32-bit (network byte order).
	 */
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERV_PORT);
	server_addr.sin_addr.s_addr = inet_addr(SERV_IP);
	
	/* =========================================================================
	 * Step 3: Giao tiếp với Server (Gửi chuỗi và nhận lại phản hồi)
	 * =========================================================================
	 */
	printf("\nType to send: ");
	memset(buff, 0, sizeof(buff));       /* Khởi tạo toàn bộ bộ đệm về ký tự null '\0' */
	if (fgets(buff, BUFF_SIZE, stdin) == NULL) {
		close(client_sock);
		return 0;
	}
	
	sin_size = sizeof(struct sockaddr_in);
	
	/*
	 * Hàm sendto(): Gửi gói tin dữ liệu tới server qua UDP.
	 * (Lưu ý: UDP là giao thức phi kết nối nên sử dụng sendto thay vì send thông thường).
	 *   - Vào:
	 *       + client_sock: Socket descriptor của client.
	 *       + buff: Con trỏ vùng đệm chứa nội dung cần gửi.
	 *       + strlen(buff): Số lượng byte cần gửi đi.
	 *       + 0: Cờ gửi tin mặc định.
	 *       + (struct sockaddr*)&server_addr: Địa chỉ đích của server.
	 *       + sin_size: Kích thước của struct server_addr.
	 *   - Ra: 
	 *       + Số byte gửi thành công.
	 *       + < 0 nếu có lỗi khi gửi.
	 */
	bytes_sent = sendto(client_sock, buff, strlen(buff), 0, (struct sockaddr *)&server_addr, sin_size);
	if (bytes_sent < 0) {
		perror("Error: ");
		close(client_sock);
		return 0;
	}

	/*
	 * Hàm recvfrom(): Chờ nhận gói tin phản hồi từ Server.
	 *   - Vào:
	 *       + client_sock: Socket descriptor của client.
	 *       + buff: Con trỏ vùng đệm lưu dữ liệu nhận về.
	 *       + BUFF_SIZE - 1: Số byte tối đa đọc vào (để chừa 1 byte kết thúc chuỗi '\0').
	 *       + 0: Cờ nhận tin mặc định.
	 *       + (struct sockaddr*)&server_addr: Nơi lưu địa chỉ server phản hồi.
	 *       + &sin_size: Con trỏ kích thước của struct server_addr.
	 *   - Ra:
	 *       + Số byte nhận được thực tế.
	 *       + < 0 nếu có lỗi khi nhận.
	 */
	bytes_received = recvfrom(client_sock, buff, BUFF_SIZE - 1, 0, (struct sockaddr *)&server_addr, &sin_size);
	if (bytes_received < 0) {
		perror("Error: ");
		close(client_sock);
		return 0;
	}

	buff[bytes_received] = '\0';        /* Đảm bảo chuỗi nhận được kết thúc an toàn */
	printf("Reply from server: %s", buff);
		
	/* Đóng socket sau khi trao đổi dữ liệu hoàn tất */
	close(client_sock);
	return 0;
}
