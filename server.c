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
	sprintf(message, "REG|%ld|%s|", s_len, s); //replace ld with zu?

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

// sends error message, using err_code type
void send_err(int sock, int n, enum err_code err) {
	const char *err_msgs[] = {"CT", "LN", "FT"};

	char message[10]; //ERR + 4 error code length + 2 seperators + 1 null
	sprintf(message, "ERR|M%d%s|", 2*n+1, err_msgs[err]);

	send(sock, message, 10, 0);
}

// len is length of received string
bool parse_kkj(int sock, char *m, char *setup, int n, bool (*check_msg) (char *, char *, char *)) {
	if (strlen(m) < 7) {
		send_err(sock, n, FT); //checks if contains the minimum character number....but maybe this should be
		return false;
	}
	//check for proper header, return format error
	printf("check header\n");
	if (strncmp(m, "REG|", 4) != 0) {
		send_err(sock, n, FT);
		return false;
	}

	char *len_ptr = m+4; //increment pointer by 4 to find part past the seperator
	printf("check second sep\n");
	char *msg_ptr = strchr(len_ptr + 1, '|');
	if (msg_ptr == NULL) {
		send_err(sock, n, FT); //send error is message is not found
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
//from here until END lines may have been inserted/deleted
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

bool check_err(char* str, char* end, int n) {
	if (str == end) {
		// message error (message empty)
		return false;
	}
	bool first_two = (*str == 'M') && *(str+1) == (2*n)+'0';
	bool last_two = (strncmp(str+2, "CT|", 3) == 0 || strncmp(str+2, "LN|", 3) == 0 || strncmp(str+2, "FT|", 3) == 0);
	return first_two && last_two;
}

// true means no problem, continue
bool parse_error_code(int sock, char *m, char *setup, int n) {
	if (strncmp(m, "ERR|", 4) != 0) {
		return true;
	}

	if (strlen(m) != 10) {
		send_err(sock, n, FT); //checks if contains the correct length
		return false;
	}

	char *err_str_ptr = m+4; //increment pointer by 4 to find part past the seperator
	if (err_str_ptr == NULL) {
		send_err(sock, n, FT); //not sure what error to include, likely formatting
		printf("Error code not found\n");
		return false;
	}

	char *err_end_ptr = strchr(err_str_ptr + 1, '|');
	if (err_end_ptr == NULL || *(err_str_ptr+1) != 0) {  // check that '|' is last char
		printf("Error message doesn't end with |");
		send_err(sock, n, FT);
		return false;
	}

	if ((int)(err_end_ptr - err_str_ptr) != 4) {
		send_err(sock, n, LN);
		return false;
	}
	
	bool valid = check_err(err_str_ptr, err_end_ptr, n);
	if (!valid) {
		send_err(sock, n, CT);
		return false;
	}

	printf("Recieved unidentified error from client\n");
	send_err(sock, n, CT);
	return false;
}

int main(int argc, char **argv) { //maybe check if argc=1
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(strtol(argv[1], NULL, 0));

	if (bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
		printf("error binding");
	}

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

	bool error_raised = false;
	bool (*msg_checks[])(char *, char *, char *) = {&check_1, &check_2, &check_3};
	while (!error_raised) {
		listen(server_sock, 10);
		int client_sock = accept(server_sock, NULL, NULL);

		for (int i=0; i < 3; i++) {
			printf("%d\n", i);
			send_kkj(client_sock, prompts[i]);
			read_into_buf(client_sock, &buffer);

			if (!parse_error_code(client_sock, buffer.buf, setup, i)) {
				error_raised = true;
				break;
			}

			if (!parse_kkj(client_sock, buffer.buf, setup, i, msg_checks[i])) {
				printf("Invalid client msg: %s\n", buffer.buf);
				error_raised = true;
				break;
			}
		}
	}
	// read "who's there" and then send follow up
	close(server_sock);
	free(buffer.buf);
	return 0;
}