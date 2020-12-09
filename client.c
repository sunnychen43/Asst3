#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
 
#include <sys/types.h>
#include <sys/socket.h>
 
#include <fcntl.h> // for open
#include <unistd.h> // for close
 
#include <arpa/inet.h>
#include <netinet/in.h>

int digits(int num) {
   int count = 0;
   while(num != 0) {
      num /= 10;
      count++;
   }
   return count;
}

char *format_kkj(char *s) {
    size_t s_len = strlen(s);
    size_t m_len = 7 + digits(s_len) + s_len; // 7 = REG + 3 seperators + 1 null char

    char *message = malloc(m_len);
    sprintf(message, "REG|%ld|%s|", s_len, s);

    return message;
}

int main(int argc, char **argv) {
    struct sockaddr_in server_addr;

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(strtol(argv[1], NULL, 0));

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("connect failed. Error");
		return 1;
	}

	char server_buf[256];
    while (true) {
		printf("reading\n");
		int b_read = read(sock, server_buf, 256);
		server_buf[b_read] = 0;
		printf("%s\n", server_buf);

        char buf[256];
        fgets(buf, 255, stdin);
		buf[strcspn(buf, "\n")] = 0;

		char *response = format_kkj(buf);

		write(sock, response, strlen(response));
    }

    close(sock);

    return 0;
}