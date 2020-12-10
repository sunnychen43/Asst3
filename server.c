#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <fcntl.h> // for open
#include <unistd.h> // for close

#include <netinet/in.h>

typedef struct Buffer {
	char *buf;

	int curr_size;
	int max_size;

} Buffer;

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

void send_kkj(int sock, char *s) {
	char *message = format_kkj(s);
	size_t m_len = strlen(message)+1;

	send(sock, message, m_len, 0);
	free(message);
}

enum err_code {
	CT,  // content
	LN,  // length
	FT   // format
};

void send_err(int sock, int n, enum err_code err) {
	const char *err_msgs[] = {"CT", "LN", "FT"};

	char message[10]; //ERR + 4 error code length + 2 seperators + 1 null
	sprintf(message, "ERR|M%d%s|", 2*n+1, err_msgs[err]);

	send(sock, message, 10, 0);
}

// len is length of received string
bool parse_kkj(int sock, char *m, char *setup, int n, bool (*check_msg) (char *, char *, char *)) {
	if (strlen(m) < 7) {
		send_err(sock, n, FT);
		return false;
	}

	printf("check header\n");
	if (strncmp(m, "REG|", 4) != 0) {
		send_err(sock, n, FT);
		return false;
	}

	char *len_ptr = m+4;
	printf("check second sep\n");
	char *msg_ptr = strchr(len_ptr + 1, '|');
	if (msg_ptr == NULL) {
		send_err(sock, n, FT);
		return false;
	}

	printf("check third sep\n");
	// REG|3|dog|
	char *end_ptr = strchr(msg_ptr + 1, '|');
	printf("%p\n", end_ptr);
	if (end_ptr == NULL || *(end_ptr+1) != 0) {  // check that '|' is last char
		send_err(sock, n, FT);
		return false;
	}

	printf("check len\n");
	// check if number
	for (char *p = len_ptr; p < msg_ptr; p++) {
		if (!isdigit(*p)) {
			send_err(sock, n, FT);
			return false;
		}
	}

	printf("check len match\n");
	long msg_len = strtol(len_ptr, NULL, 10);
	if (msg_len != (int)(end_ptr - msg_ptr) - 1) {
		send_err(sock, n, LN);
		return false;
	}

	printf("check msg\n");
	bool valid = check_msg(msg_ptr+1, end_ptr, setup);
	if (!valid) {
		send_err(sock, n, CT);
		return false;
	}

	return true;
}

/*
start begins at the first charactere and end points to |, and is followed by the null terminator
checks is message is "Who's there?"
*/ 
bool check_1 (char *start, char *end, char *setup) {
	if (start == end) {
		// message error (message empty)
		return false;
	}
	printf("%s\n", start);
	return (strcmp(start, "Who's there?|") == 0);
}

/*
start begins at the first charactere and end points to |, and is followed by the null terminator
checks if message is previous message, followed by ", who?"
*/ 
bool check_2 (char *start, char *end, char *setup) {
	if (start == end) {
		// message error (message empty)
		return false;
	}
	int n = strlen(setup); // char is 1 byte
	printf("%s %s %d\n", start, setup, n);
	return (strncmp(start, setup, n) == 0 && strcmp(start + n, ", who?|") == 0);
}

bool check_3(char *start, char *end, char *setup) {
	char c = *(end-1);
	bool is_punc = (strchr(".?!", c) != NULL);

	return (int)(end-start) >= 2 && is_punc;
}

void read_into_buf(int sock, Buffer *buffer) {
	buffer->curr_size = 0;

	int bytes_read;
	do {
		// resize if not enough room
		if (buffer->max_size - buffer->curr_size - 1 < 256) {
			buffer->buf = realloc(buffer->buf, buffer->max_size * 2);
			buffer->max_size *= 2;
		}

		bytes_read = recv(sock, buffer->buf, 256, 0);
		buffer->curr_size += bytes_read;

	} while (bytes_read == 256);

	buffer->buf[buffer->curr_size] = 0;  // add null char
}

int main(int argc, char **argv) {
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(strtol(argv[1], NULL, 0));

	if (bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
		printf("error binding");
	}
	listen(server_sock, 10);
	int client_sock = accept(server_sock, NULL, NULL);

	Buffer buffer;
	buffer.buf = (char *)malloc(256);
	buffer.curr_size = 0;
	buffer.max_size = 512;

	// sends "knock knock"
	char setup[20] = "Dragon";
	char prompts[3][40];

	strcpy(prompts[0], "Knock, knock.");

	strcpy(prompts[1], setup);
	strcat(prompts[1], ".");

	strcpy(prompts[2], setup);
	strcat(prompts[2], " these nuts across your face.");


	bool (*msg_checks[])(char *, char *, char *) = {&check_1, &check_2, &check_3};

	for (int i=0; i < 3; i++) {
		printf("%d\n", i);
		send_kkj(client_sock, prompts[i]);
		read_into_buf(client_sock, &buffer);

		if (strncmp(buffer.buf, "ERR|", 4) == 0) {
			printf("Recieved err from client\n");
			break;
		}

		if (!parse_kkj(client_sock, buffer.buf, setup, i, msg_checks[i])) {
			printf("Invalid client msg: %s\n", buffer.buf);
			break;
		}
	}
	// read "who's there" and then send follow up
	close(server_sock);
	return 0;
}