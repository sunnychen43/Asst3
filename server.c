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


/*-----------------------------Network IO-------------------------------------*/

/* Buffer for receiving from client */
typedef struct Buffer {
	char *buf;  // stores data
	int curr_size;  // how many bytes are in use
	int max_size;  // how many bytes allocated to buf
} Buffer;


/* counts number of digits, used for verifying msg length */
int digits(int num) {
	int count = 0;
	while(num != 0) {
		num /= 10;
		count++;
	}
	return count;
}


/* converts string to kkj formatted message */
char *format_kkj(char *s) {
	size_t s_len = strlen(s);
	size_t m_len = 7 + digits(s_len) + s_len; // kkj message length, 7 = REG + 3 seperators + 1 null char

	char *message = malloc(m_len);
	sprintf(message, "REG|%ld|%s|", s_len, s);  // write formatted message to string

	return message;
}


/* formats and sends message to client socket */
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


/* Reads a message of unknown length from client. Resize buffer if message goes over
allocated memory. */
void read_into_buf(int sock, Buffer *buffer) {
	buffer->curr_size = 0;  // this clears buffer, we will overwrite data already in buf

	int bytes_read;
	do {
		// check if buf has enough room before reading
		if (buffer->max_size - buffer->curr_size - 1 < 256) {  // save one byte for null terminator
			buffer->buf = realloc(buffer->buf, buffer->max_size * 2);  // double buf size
			buffer->max_size *= 2;
		}

		bytes_read = recv(sock, buffer->buf+buffer->curr_size, 256, 0);  // writes to first free bytes
		buffer->curr_size += bytes_read;

	} while (bytes_read == 256);

	buffer->buf[buffer->curr_size] = 0;  // add null char
}


/*-----------------------------Parsing Messages-------------------------------------*/

// len is length of received string
bool parse_reg(int sock, char *m, char *setup, int n, bool (*check_msg) (char *, char *, char *)) {
	if (strlen(m) < 7) {
		send_err(sock, n, FT); //checks if contains the minimum character number....but maybe this should be
		return false;
	}
	//check for proper header, return format error
	if (strncmp(m, "REG|", 4) != 0) {
		send_err(sock, n, FT);
		return false;
	}

	char *len_ptr = m+4; //increment pointer by 4 to find part past the seperator
	char *msg_ptr = strchr(len_ptr + 1, '|');
	if (msg_ptr == NULL) {
		send_err(sock, n, FT); //send error is message is not found
		return false;
	}

	// REG|3|dog|
	char *end_ptr = strchr(msg_ptr + 1, '|');
	if (end_ptr == NULL || *(end_ptr+1) != 0) {  // check that '|' is last char
		send_err(sock, n, FT);
		return false;
	}

	// check if number
	for (char *p = len_ptr; p < msg_ptr; p++) {
		if (!isdigit(*p)) {
			send_err(sock, n, FT);
			return false;
		}
	}

	long msg_len = strtol(len_ptr, NULL, 10);
	if (msg_len != (int)(end_ptr - msg_ptr) - 1) {
		send_err(sock, n, LN);
		return false;
	}

	bool valid = check_msg(msg_ptr+1, end_ptr, setup);
	if (!valid) {
		send_err(sock, n, CT);
		return false;
	}

	return true;
}

/* A client should only ever send 3 types of messages. These functions check the content of each one. */

/* simple check for "Who's there?|" */
bool check_1 (char *start, char *end, char *setup) {
	if (start == end) {
		// message error (message empty)
		return false;
	}
	return (strcmp(start, "Who's there?|") == 0);
}

/* checks for "<setup>, who?|" */
bool check_2 (char *start, char *end, char *setup) {
	if (start == end) {
		// message error (message empty)
		return false;
	}
	int n = strlen(setup); // char is 1 byte
	return (strncmp(start, setup, n) == 0 && strcmp(start + n, ", who?|") == 0);
}

/* checks for any char and then punctuation */
bool check_3(char *start, char *end, char *setup) {
	char c = *(end-1);
	bool is_punc = (strchr(".?!", c) != NULL);

	return (int)(end-start) >= 2 && is_punc;
}


/* makes sure that the error code is the right format. we assume 
that an incorrect code is a FORMAT error, not a CONTENT error */

bool check_err_code(char* str, char* end, int n) {
	bool first_two = (*str == 'M') && *(str+1) == (2*n)+'0';
	bool last_two = (strncmp(str+2, "CT|", 3) == 0 || strncmp(str+2, "LN|", 3) == 0 || strncmp(str+2, "FT|", 3) == 0);

	return first_two && last_two;
}

/* Parses a message to see if it is a valid error code. If it is an error, return false. Otherwise return true. */

bool parse_error(int sock, char *m, char *setup, int n) {
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
		return false;
	}

	char *err_end_ptr = strchr(err_str_ptr + 1, '|');
	if (err_end_ptr == NULL || *(err_str_ptr+1) != 0) {  // check that '|' is last char
		send_err(sock, n, FT);
		return false;
	}

	if ((int)(err_end_ptr - err_str_ptr) != 4) {
		send_err(sock, n, LN);
		return false;
	}
	
	bool valid = check_err_code(err_str_ptr, err_end_ptr, n);
	if (!valid) {
		send_err(sock, n, CT);
		return false;
	}

	send_err(sock, n, CT);
	return false;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Invalid argument count, only include server port.\n");
		return -1;
	}

	int port = strtol(argv[1], NULL, 0);
	if (port < 9000) {
		printf("Invalid port. Port must be between 9000 - 65535\n");
		return -1;
	}


	/* Initialize server socket and bind to addr */
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(strtol(argv[1], NULL, 0));

	if (bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
		printf("Unable to bind.\n");
		return -1;
	}

	/* joke setup string */
	char setup[20] = "Dragon";

	/* store the prompts to send to client, 3 in total */
	char prompts[3][40];
	strcpy(prompts[0], "Knock, knock.");
	strcpy(prompts[1], setup);
	strcat(prompts[1], ".");
	strcpy(prompts[2], setup);
	strcat(prompts[2], " these nuts across your face.");

	Buffer buffer;  // buffer for client data
	bool (*msg_checks[])(char *, char *, char *) = {&check_1, &check_2, &check_3};  // function pointers to check msg content

	bool error_raised = false;  
	while (!error_raised) {

		printf("Listening for client.\n");
		if (listen(server_sock, 10) == -1) {
			printf("Timeout, no client connected.\n");
			break;
		}

		/* Initialize buffer, can initally hold 512 bytes */
		buffer.buf = (char *)malloc(512);
		buffer.curr_size = 0;
		buffer.max_size = 512;

		int client_sock = accept(server_sock, NULL, NULL);
		printf("Connected to client.\n");

		/* loop through 3 prompts */
		for (int i=0; i < 3; i++) {
			printf("Sending: %s\n", prompts[i]);
			send_kkj(client_sock, prompts[i]);

			read_into_buf(client_sock, &buffer);
			printf("Received: %s\n", buffer.buf);

			/* if client sent error message, terminate */
			if (!parse_error(client_sock, buffer.buf, setup, i)) {
				printf("Recieved error from client.\n");
				error_raised = true;
				break;
			}

			/* if client message is invalid, terminate */
			if (!parse_reg(client_sock, buffer.buf, setup, i, msg_checks[i])) {
				printf("Invalid client msg.\n");
				error_raised = true;
				break;
			}
		}

		printf("Closing connection to client.\n");
		close(client_sock);
		free(buffer.buf);
	}

	printf("Closing server.\n");
	close(server_sock);
	return 0;
}