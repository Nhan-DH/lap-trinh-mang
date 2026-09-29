/*
 * server.c - Server trao đổi dữ liệu với tối đa hai client bằng UDP.
 * UDP không tạo kết nối vật lý; server nhận endpoint từ recvfrom() và dùng
 * sendto() để trả lời hoặc chuyển tiếp dữ liệu cho client còn lại.
 */

#include <arpa/inet.h>      /* inet_ntop(), htons(), ntohs(): xử lý IP và byte order */
#include <errno.h>          /* errno, EINTR: mã lỗi và lỗi system call bị ngắt */
#include <netinet/in.h>     /* sockaddr_in, INADDR_ANY: địa chỉ mạng IPv4 */
#include <signal.h>         /* signal(), SIGINT: xử lý tín hiệu dừng server */
#include <stdio.h>          /* printf(), fprintf(), perror(): vào/ra và in lỗi */
#include <stdlib.h>         /* atoi(), exit(), EXIT_SUCCESS, EXIT_FAILURE */
#include <string.h>         /* memset(), strlen(), strcmp(), strcspn() */
#include <sys/socket.h>     /* socket(), bind(), recvfrom(), sendto() */
#include <unistd.h>         /* close(): đóng file descriptor */

/* =========================================================================
 * Phần 1: Khai báo hằng số và kiểu dữ liệu dùng chung trong Server
 * =========================================================================
 */
#define PORT 5550                         /* Port mặc định của UDP Server */
#define MAX_CLIENTS 2                     /* Số client tối đa server phục vụ */
#define MAX_DATAGRAM_SIZE 1024            /* Kích thước tối đa của một datagram */
#define CONNECT_TOKEN "__CONNECT__"       /* Token client gửi để đăng ký với server */
#define CONNECTED_TOKEN "__CONNECTED__"   /* Thông báo server gửi khi đăng ký thành công */
#define EXIT_TOKEN_AT "@"                 /* Token yêu cầu ngắt kết nối dạng @ */
#define EXIT_TOKEN_HASH "#"               /* Token yêu cầu ngắt kết nối dạng # */
#define INVALID_STRING_MESSAGE "Error: String contains invalid characters! Only alphanumeric characters are allowed." /* Lỗi chuỗi không hợp lệ */
#define SERVER_FULL_MESSAGE "Error: Server is full (maximum 2 clients reached)." /* Lỗi server đủ client */

typedef struct {
	int active;                      /* Bằng 1 nếu slot đang được một client sử dụng */
	struct sockaddr_in address;      /* Địa chỉ IPv4 và port của client */
} ClientSlot;

static volatile sig_atomic_t server_running = 1;

/* =========================================================================
 * Phần 2: Các hàm hỗ trợ quản lý Client và kiểm tra dữ liệu
 * =========================================================================
 */

/**
 * @brief Xử lý tín hiệu dừng chương trình từ hệ điều hành.
 *
 * Chỉ thay đổi cờ điều khiển để vòng lặp chính kết thúc an toàn và đóng socket.
 */
static void handle_signal(int signal_number)
{
	(void)signal_number;
	server_running = 0;
}

static int is_exit_token(const char *message)
{
	return strcmp(message, EXIT_TOKEN_AT) == 0 || strcmp(message, EXIT_TOKEN_HASH) == 0;
}

static int is_alphanumeric_string(const char *message)
{
	int index;

	if (message[0] == '\0') {
		return 0;
	}
	for (index = 0; message[index] != '\0'; index++) {
		if (!((message[index] >= 'a' && message[index] <= 'z') ||
		      (message[index] >= 'A' && message[index] <= 'Z') ||
		      (message[index] >= '0' && message[index] <= '9'))) {
			return 0;
		}
	}
	return 1;
}

static int same_client(const struct sockaddr_in *left,
	const struct sockaddr_in *right)
{
	return left->sin_addr.s_addr == right->sin_addr.s_addr &&
	       left->sin_port == right->sin_port;
}

static int find_client(ClientSlot clients[], const struct sockaddr_in *address)
{
	int index;

	for (index = 0; index < MAX_CLIENTS; index++) {
		if (clients[index].active && same_client(&clients[index].address, address)) {
			return index;
		}
	}
	return -1;
}

static int find_free_slot(ClientSlot clients[])
{
	int index;

	for (index = 0; index < MAX_CLIENTS; index++) {
		if (!clients[index].active) {
			return index;
		}
	}
	return -1;
}

/* =========================================================================
 * Phần 3: Hàm main - khởi tạo Server và xử lý các datagram UDP
 * =========================================================================
 */

/**
 * @brief Hàm chính thực thi UDP Chat Server phục vụ tối đa hai client.
 *
 * Chi tiết luồng xử lý:
 *   - Bước 1: Đọc port, tạo socket UDP và gán địa chỉ bằng bind().
 *   - Bước 2: Nhận datagram bằng recvfrom() và xác định client gửi.
 *   - Bước 3: Kiểm tra token kết nối, token thoát và tính hợp lệ của chuỗi.
 *   - Bước 4: In chuỗi hợp lệ lên server và chuyển tiếp cho client còn lại.
 *   - Bước 5: Đóng socket khi server nhận tín hiệu kết thúc.
 *
 * @param argc Số lượng đối số dòng lệnh.
 * @param argv argv[1] là port UDP server; nếu bỏ qua thì dùng PORT mặc định.
 * @return int Trả về 0 khi kết thúc thành công, khác 0 khi tham số không hợp lệ.
 */
int main(int argc, char *argv[])
{
	int server_sock;                  /* File descriptor của socket server */
	char buff[MAX_DATAGRAM_SIZE];     /* Bộ đệm lưu trữ dữ liệu nhận và gửi */
	ssize_t bytes_received;            /* Số lượng byte thực tế đã nhận */
	struct sockaddr_in server;        /* Cấu trúc lưu thông tin địa chỉ của server */
	struct sockaddr_in client;        /* Cấu trúc lưu thông tin địa chỉ của client gửi đến */
	socklen_t sin_size;               /* Kích thước của cấu trúc sockaddr_in */
	ClientSlot clients[MAX_CLIENTS] = {{0}}; /* Danh sách slot của các client */
	int client_index;                         /* Vị trí slot của client đang gửi */
	char forwarded_message[MAX_DATAGRAM_SIZE + 64]; /* Chuỗi có thêm IP và port */
	char address_text[INET_ADDRSTRLEN];       /* Địa chỉ IP dạng chuỗi để hiển thị */
	int port;                                 /* Port server lấy từ dòng lệnh */
	char *end_pointer;                        /* Vị trí kết thúc chuỗi port */
	long parsed_port;                         /* Giá trị port sau khi chuyển đổi */

	/* Server phải nhận đúng một đối số là port; không dùng port mặc định. */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <PortNumber>\n", argv[0]);
		return EXIT_FAILURE;
	}
	errno = 0;
	parsed_port = strtol(argv[1], &end_pointer, 10);
	if (errno != 0 || argv[1][0] == '\0' || *end_pointer != '\0' ||
	    parsed_port < 1 || parsed_port > 65535) {
		fprintf(stderr, "Usage: %s <PortNumber>\n", argv[0]);
		return EXIT_FAILURE;
	}
	port = (int)parsed_port;

	/* =========================================================================
	 * Step 1: Khởi tạo socket UDP
	 * =========================================================================
	 * Hàm socket():
	 *   - Vào:
	 *       + AF_INET: Giao thức IPv4.
	 *       + SOCK_DGRAM: Giao thức UDP (hướng datagram, không kết nối).
	 *       + 0: Giao thức mặc định tương ứng với SOCK_DGRAM (IPPROTO_UDP).
	 *   - Ra: File descriptor không âm nếu thành công, -1 nếu lỗi.
	 *   - Lỗi: socket() thất bại khi hệ điều hành không cấp phát được socket.
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
	 *     INADDR_ANY được đặt ở network byte order khi bind trên hệ thống Linux.
	 *   - bzero: Xóa phần đệm sin_zero của struct sockaddr_in về 0.
	 */
	server.sin_family = AF_INET;         
	server.sin_port = htons(port);
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
	if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) == -1) {
		perror("\nError: ");
		exit(0);
	}     
	
	/* =========================================================================
	 * Step 3: Nhận và phân loại datagram từ các Client
	 * =========================================================================
	 */
	signal(SIGINT, handle_signal);
	printf("[SERVER] UDP Server is running and listening at port %d...\n", port);

	while (server_running) {
		sin_size = sizeof(struct sockaddr_in);
    		
		/*
		 * Hàm recvfrom(): Chờ và nhận một UDP datagram từ một client bất kỳ.
		 *   - Vào:
		 *       + server_sock: Socket descriptor nhận dữ liệu.
		 *       + buff: Con trỏ vùng đệm lưu dữ liệu nhận được.
		 *       + MAX_DATAGRAM_SIZE - 1: Kích thước tối đa có thể đọc, chừa 1 byte cho '\0'.
		 *       + 0: Cờ điều khiển (flags), 0 là chế độ nhận dữ liệu mặc định.
		 *       + (struct sockaddr*)&client: Con trỏ lưu thông tin IP/port của client gửi tới.
		 *       + &sin_size: Con trỏ chứa kích thước ban đầu của struct client.
		 *   - Ra:
		 *       + Số byte nhận được thực tế nếu thành công.
		 *       + -1 nếu có lỗi xảy ra; errno cho biết nguyên nhân lỗi.
		 *   - recvfrom() đồng thời ghi địa chỉ IP và port của endpoint gửi vào client.
		 */
		bytes_received = recvfrom(server_sock, buff, MAX_DATAGRAM_SIZE - 1, 0, (struct sockaddr *)&client, &sin_size);
		
		if (bytes_received < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("recvfrom");
			continue;
		}
		buff[bytes_received] = '\0';
		buff[strcspn(buff, "\r\n")] = '\0';
		client_index = find_client(clients, &client);

		/* Client mới phải gửi token kết nối trước khi gửi dữ liệu. */
		if (client_index < 0 && strcmp(buff, CONNECT_TOKEN) == 0) {
			int free_slot = find_free_slot(clients);
			if (free_slot < 0) {
				/* sendto(): Gửi thông báo server đầy về đúng client đang yêu cầu kết nối. */
				(void)sendto(server_sock, SERVER_FULL_MESSAGE, strlen(SERVER_FULL_MESSAGE), 0,
				             (struct sockaddr *)&client, sin_size);
			} else {
				clients[free_slot].active = 1;
				clients[free_slot].address = client;
				/* sendto(): Xác nhận đăng ký thành công cho client mới. */
				(void)sendto(server_sock, CONNECTED_TOKEN, strlen(CONNECTED_TOKEN), 0,
				             (struct sockaddr *)&client, sin_size);
			}
			continue;
		}
		if (client_index < 0) {
			continue;
		}
		if (is_exit_token(buff)) {
			clients[client_index].active = 0;
			continue;
		}
		if (!is_alphanumeric_string(buff)) {
			(void)sendto(server_sock, INVALID_STRING_MESSAGE, strlen(INVALID_STRING_MESSAGE), 0,
			             (struct sockaddr *)&client, sin_size);
			continue;
		}

		/* =========================================================================
		 * Step 4: Kiểm tra, in và chuyển tiếp chuỗi hợp lệ
		 * =========================================================================
		 */
		/* Chuỗi không hợp lệ đã được phản hồi ở trên và không được in ra server. */
		if (inet_ntop(AF_INET, &client.sin_addr, address_text, sizeof(address_text)) == NULL) {
			continue;
		}
		printf("[%s:%d]: %s\n", address_text, ntohs(client.sin_port), buff);
		/* Đẩy ngay nội dung ra stdout để log không bị mất khi server nhận SIGINT. */
		fflush(stdout);
		(void)snprintf(forwarded_message, sizeof(forwarded_message), "[%s:%d]: %s\n",
		               address_text, ntohs(client.sin_port), buff);
		/*
		 * Hàm sendto(): Chuyển datagram tới client còn lại qua UDP.
		 *   - Vào:
		 *       + server_sock: Socket descriptor của server.
		 *       + forwarded_message: Nội dung gồm IP, port và chuỗi hợp lệ.
		 *       + clients[index].address: Địa chỉ đích của client còn lại.
		 *   - Ra: Số byte đã gửi hoặc -1 nếu xảy ra lỗi.
		 */
		for (int index = 0; index < MAX_CLIENTS; index++) {
			if (index != client_index && clients[index].active) {
				(void)sendto(server_sock, forwarded_message, strlen(forwarded_message), 0,
				             (struct sockaddr *)&clients[index].address, sizeof(clients[index].address));
			}
		}
	}
	
	/* =========================================================================
	 * Step 5: Đóng socket và kết thúc Server
	 * =========================================================================
	 * close(): Giải phóng file descriptor sau khi vòng lặp nhận dữ liệu kết thúc.
	 */
	close(server_sock);
	return 0;
}
