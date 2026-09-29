/*
 * UDP Chat Client
 * 
 * Mô tả:
 *   Chương trình UDP Client gửi chuỗi do người dùng nhập tới UDP Server,
 *   đồng thời nhận và hiển thị tin nhắn do client còn lại gửi qua server.
 */

#include <stdio.h>          /* Thư viện vào/ra chuẩn: printf, fprintf, perror, fgets */
#include <stdlib.h>         /* Thư viện chuẩn: atoi(), exit(), mã kết thúc chương trình */
#include <sys/types.h>      /* Các kiểu dữ liệu hệ thống */
#include <sys/socket.h>     /* Các hàm và cấu trúc socket: socket, sendto, recvfrom */
#include <netinet/in.h>     /* Các cấu trúc địa chỉ Internet: sockaddr_in, in_addr */
#include <arpa/inet.h>      /* inet_pton(), htons(): chuyển đổi IP và byte order */
#include <string.h>         /* memset(), strlen(), strcmp(), strcspn() */
#include <unistd.h>         /* close(): đóng file descriptor */
#include <errno.h>          /* errno, EINTR: mã lỗi system call */
#include <sys/select.h>     /* select(), fd_set: chờ nhiều nguồn dữ liệu */

/* =========================================================================
 * Phần 1: Khai báo hằng số sử dụng trong Client
 * =========================================================================
 */
#define SERV_PORT 5550      /* Cổng của UDP Server cần kết nối */
#define SERV_IP "127.0.0.1" /* Địa chỉ IP của Server (localhost) */
#define BUFF_SIZE 1024      /* Kích thước bộ đệm chứa dữ liệu */
#define CONNECT_TOKEN "__CONNECT__" /* Token đăng ký client với server */
#define EXIT_TOKEN_AT "@"            /* Token kết thúc phiên dạng @ */
#define EXIT_TOKEN_HASH "#"          /* Token kết thúc phiên dạng # */

/* =========================================================================
 * Phần 2: Hàm main - khởi tạo Client và trao đổi datagram UDP
 * =========================================================================
 */

/**
 * @brief Hàm chính thực thi UDP Chat Client.
 * 
 * Chi tiết luồng xử lý:
 *   - Bước 1: Kiểm tra IP/port, tạo socket UDP và thiết lập địa chỉ server.
 *   - Bước 2: Gửi token kết nối bằng sendto().
 *   - Bước 3: Dùng select() để chờ bàn phím hoặc datagram từ server.
 *   - Bước 4: Gửi dữ liệu bằng sendto(), nhận dữ liệu bằng recvfrom().
 *   - Bước 5: Gửi token '@' hoặc '#' và đóng socket để kết thúc.
 * 
 * @param argc Số lượng đối số dòng lệnh.
 * @param argv argv[1] là địa chỉ IPv4 của server, argv[2] là port server.
 * @return int Trả về 0 khi kết thúc phiên, khác 0 nếu có lỗi khởi tạo hoặc giao tiếp.
 */
int main(int argc, char *argv[])
{
	int client_sock;                 /* File descriptor của socket client */
	char buff[BUFF_SIZE];            /* Bộ đệm chứa thông điệp gửi và nhận */
	struct sockaddr_in server_addr;  /* Cấu trúc lưu thông tin địa chỉ server đích */
	int bytes_sent, bytes_received;  /* Số byte thực tế đã gửi hoặc nhận */
	socklen_t sin_size;              /* Kích thước của struct sockaddr_in */
	fd_set read_fds;                 /* Tập descriptor chờ dữ liệu */
	int server_port = SERV_PORT;       /* Port server, mặc định là SERV_PORT */

	/* Xóa cấu trúc trước khi dùng inet_pton() để tránh dữ liệu chưa khởi tạo. */
	memset(&server_addr, 0, sizeof(server_addr));
	if (argc == 3) {
		server_port = atoi(argv[2]);
	}
	if (argc != 3 || server_port < 1 || server_port > 65535 ||
	    inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
		fprintf(stderr, "Usage: %s <IPAddress> <PortNumber>\n", argv[0]);
		return 1;
	}
	
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
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
	/* htons(): đổi port từ host byte order sang network byte order. */
	
	/* =========================================================================
	 * Step 3: Gửi token đăng ký tới Server
	 * =========================================================================
	 */
	sin_size = sizeof(struct sockaddr_in);
	/*
	 * Hàm sendto(): Gửi token kết nối tới server qua UDP.
	 *   - Vào:
	 *       + client_sock: Socket descriptor của client.
	 *       + CONNECT_TOKEN: Chuỗi điều khiển cần gửi.
	 *       + strlen(CONNECT_TOKEN): Số byte của token, không gửi '\0'.
	 *       + 0: Cờ gửi tin mặc định.
	 *       + (struct sockaddr *)&server_addr: Địa chỉ server đích.
	 *       + sin_size: Kích thước cấu trúc địa chỉ server.
	 *   - Ra: Số byte đã gửi hoặc -1 nếu xảy ra lỗi.
	 */
	bytes_sent = sendto(client_sock, CONNECT_TOKEN, strlen(CONNECT_TOKEN), 0,
	                    (struct sockaddr *)&server_addr, sin_size);
	if (bytes_sent < 0) {
		perror("Error: ");
		close(client_sock);
		return 1;
	}

	/* =========================================================================
	 * Step 4: Giao tiếp hai chiều với Server
	 * =========================================================================
	 *
	 * Hàm select(): Chờ đồng thời dữ liệu từ bàn phím và socket UDP.
	 *   - FD_SET(STDIN_FILENO): Theo dõi khi người dùng nhập dữ liệu.
	 *   - FD_SET(client_sock): Theo dõi khi server gửi datagram tới client.
	 *   - client_sock + 1: Giá trị lớn nhất trong tập descriptor cộng 1.
	 *   - NULL ở các tập ghi, lỗi và timeout: Chỉ quan tâm descriptor đọc.
	 *   - Ra: Số descriptor sẵn sàng; -1 nếu lỗi và errno cho biết nguyên nhân.
	 *
	 * select() không thay thế UDP. Nó chỉ giúp chương trình biết thời điểm
	 * an toàn để gọi fgets() hoặc recvfrom(), nhờ đó chat có thể hai chiều.
	 */
	while (1) {
		FD_ZERO(&read_fds);
		FD_SET(STDIN_FILENO, &read_fds);
		FD_SET(client_sock, &read_fds);
		if (select(client_sock + 1, &read_fds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("select");
			break;
		}
		if (FD_ISSET(client_sock, &read_fds)) {
			/*
			 * Hàm recvfrom(): Nhận một UDP datagram từ server.
			 *   - client_sock: Socket nhận dữ liệu.
			 *   - buff: Bộ đệm lưu payload; chừa 1 byte cho '\0'.
			 *   - BUFF_SIZE - 1: Số byte tối đa được phép ghi vào buff.
			 *   - 0: Không dùng cờ đặc biệt.
			 *   - NULL, NULL: Không cần lưu lại địa chỉ nguồn vì đã biết server.
			 *   - Ra: Số byte nhận được hoặc -1 nếu xảy ra lỗi.
			 */
			bytes_received = recvfrom(client_sock, buff, BUFF_SIZE - 1, 0, NULL, NULL);
			if (bytes_received < 0) {
				perror("recvfrom");
				break;
			}
			buff[bytes_received] = '\0';
			printf("%s\n", buff);
			fflush(stdout);
		}
		if (FD_ISSET(STDIN_FILENO, &read_fds)) {
			if (fgets(buff, BUFF_SIZE, stdin) == NULL) {
				break;
			}
			buff[strcspn(buff, "\r\n")] = '\0';
			if (strcmp(buff, EXIT_TOKEN_AT) == 0 || strcmp(buff, EXIT_TOKEN_HASH) == 0) {
				/* Gửi token thoát để server giải phóng slot của client này. */
				(void)sendto(client_sock, buff, strlen(buff), 0,
				             (struct sockaddr *)&server_addr, sin_size);
				close(client_sock);
				return 0;
			}
			/*
			 * Hàm sendto(): Gửi nội dung người dùng nhập tới server qua UDP.
			 *   - buff: Payload cần gửi; strlen(buff) không bao gồm '\0'.
			 *   - server_addr: Địa chỉ IPv4 và port đích.
			 *   - Ra: Số byte đã gửi hoặc -1 nếu xảy ra lỗi.
			 */
			if (sendto(client_sock, buff, strlen(buff), 0,
			           (struct sockaddr *)&server_addr, sin_size) < 0) {
				perror("sendto");
				break;
			}
		}
	}

	/* =========================================================================
	 * Step 5: Đóng socket và kết thúc Client
	 * =========================================================================
	 */
	close(client_sock);
	return 0;
}
