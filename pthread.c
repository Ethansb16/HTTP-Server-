#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <pthread.h>
#include <getopt.h>


void decode_percent(char *encoded, char *decoded) {
	while (*encoded != '\0') {
		if (*encoded == '%' && *(encoded + 1) == '2') {
			*decoded = ' '; 
			encoded += 3; 
		} else if (*encoded == '%' && *(encoded + 1) == '0') {
			*decoded = '\n'; 
			encoded += 3; 
		} else {
			*decoded = *encoded; 
			encoded++;
		}
		decoded++; 
	}
	*decoded = '\0'; 
}

void cgi_bin(int client_socket, char *path) {
	char *program = (char *)malloc(256); 
	char *encoded = (char *)malloc(1024); 
	char *decoded = (char *)malloc(1024); 
	int i = 0; 

	while (path[i] != '?' && path[i] != '\0') {
		program[i] = path[i]; 
		i++;
	}
	program[i] = '\0'; 

	if (path[i] == '?') {
		strcpy(encoded, path + i + 1); 
	} else {
		encoded[0] = '\0'; 
	}
	
	decode_percent(encoded, decoded); 

	//set up pipes 
	int pipe_in[2], pipe_out[2];

	if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0) {
		printf("pipe error\n");
		free(program); 
		free(encoded); 
		free(decoded); 
		return; 
	}

	
	pid_t pid = fork(); 

	if (pid < 0) {
		printf("failed to fork\n");
		free(program); 
		free(encoded); 
		free(decoded); 
		close(client_socket); 
		return;

	/*directing pipes for child*/

	} else if (pid == 0) {
		close(pipe_in[1]); 
		close(pipe_out[0]); 

	/* linking outputs/inputs of program*/
		dup2(pipe_in[0], STDIN_FILENO); 
		dup2(pipe_out[1], STDOUT_FILENO); 
		dup2(pipe_out[1], STDERR_FILENO);

		close(pipe_in[0]); 
		close(pipe_out[1]); 
	
		char *my_argv[] = {program, NULL}; 
		
	//	printf("executing program: %s\n", program); 
		//char *testing = "/usr/bin/"; 
		char exec_path[256]; 

		snprintf(exec_path, sizeof(exec_path), "%s", program); 

		//strcat(testing, program);
		int execret = execv(exec_path, my_argv);

		//printf("testing\n"); 
		//exit(1); 
		printf("failed to exec %i\n", execret); 
		return;

	} else {//parent  
	//may need to pause
		close(pipe_in[0]); 
		close(pipe_out[1]);
		
		//write to stdin the decoded input
		printf("Decoded: %s\n", decoded); 
		write(pipe_in[1], decoded, strlen(decoded)); 
		close(pipe_in[1]); 
		
		//read the stdout
		char buffer[1024]; 
		int bytes_read; 
		char header[256]; 
		char body[1024]; 
		int length = 0; 
	
	/* read output of child into body*/

		while ((bytes_read = read(pipe_out[0], buffer, sizeof(buffer))) > 0) {
			memcpy(body + length, buffer, bytes_read); 
			length += bytes_read; 
		}
		printf("length = %i\n", length); 
		close(pipe_out[0]); 

		printf("body content: %s\n", body); 
		int status; 

		waitpid(pid, &status, 0); 

		snprintf(header, 256, 
			"HTTP/1.1 200 OK\r\n" 
			"Content-Type: text/html\r\n"
			"Content-Length: %i\r\n\r\n", 
			length);
	
		send(client_socket, header, strlen(header), 0); 
		send(client_socket, body, length, 0); 

		close(client_socket); 
	}

	free(program); 
	free(encoded); 
	free(decoded); 
}

void get_req(int client_socket, char *filename) {
	FILE *openfile = fopen(filename, "r");

	if (openfile != NULL) {
		char get_buf[1024]; 
		int bytes; 

		while ((bytes = fread(get_buf, 1, 1024, openfile)) > 0) {
			send(client_socket, get_buf, bytes, 0); 
		}
		fclose(openfile); 
	} else {
		char *response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";

		send(client_socket, response, strlen(response + 1), 0);
		close(client_socket); 
		return;
	}
	close(client_socket); 
}

void *handle_client_request(void *param) {
	int client_socket = *((int *) param);
	char buf[1024];
	
//	free(param); 
	memset(buf, 0, 1024);	
	ssize_t bytes_read = recv(client_socket, buf, sizeof(buf) - 1, 0); 
	
	if (bytes_read <= 0) {
		fprintf(stderr, "Error reading client request\n"); 
		close(client_socket);
		return NULL; 
	}
	buf[bytes_read] = '\0'; //remove CR/NL
	printf("received: %s\n", buf);// received: GET /test_file.txt HTTP/1.1

	char *type = strtok(buf, " "); 
	char *file = strtok(NULL, " "); 
	
	if (type == NULL || file == NULL || strtok(NULL, " ") == NULL) {
		char *response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";

		send(client_socket, response, strlen(response), 0); 	
		close(client_socket); 
		return NULL; 	
	}

	if (strstr(file, "..") != NULL) {
		char *response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
		send(client_socket, response, strlen(response), 0); 	
		close(client_socket); 
		return NULL; 
	}

	if (file[0] == '/') {
		file++;
	}

	if (strncmp(file, "delay/", 6) == 0) {
		int delay_time = atoi(file + 6); 

		sleep(delay_time);
		char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";

		send(client_socket, response, strlen(response), 0); 
		close(client_socket);
		return NULL;
	}

	if (strcmp(type, "HEAD") != 0 && strcmp(type, "GET") != 0) {
		char *response; 
		if (strcmp(type, "DELETE") == 0) {
			response = "HTTP/1.1 501 Not Implemented\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";

		} else {
			response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
		}
		send(client_socket, response, strlen(response), 0); 	
		close(client_socket); 
		return NULL;
	}

	if (strncmp(file, "cgi-bin/", 8) != 0) {
		if (strcmp(type, "GET") == 0 || strcmp(type, "HEAD") == 0) {
			FILE *openf = fopen(file, "r"); 
		
			if (openf == NULL) {
				char *response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";

				send(client_socket, response, strlen(response), 0); 
				close(client_socket); 
				return NULL; 
			}
			fclose(openf); 
		} else {
			char *response = "HTTP/1.1 501 Not Implemented\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";

			send(client_socket, response, strlen(response), 0); 
			close(client_socket); 
			return NULL; 

		}
	}

	char header[1024];
	struct stat file_stat;


	if (stat(file, &file_stat) < 0 && strncmp(file, "cgi-bin/", 8) != 0) {
		perror("stat"); 
		char *response = "HTTP/1.1 404 Not Found\r\nContent Type: text/html\r\n\r\n";

		send(client_socket, response, strlen(response), 0);
		close(client_socket); 
		return NULL;
	}
	if ((strcmp(type, "HEAD") == 0 || strcmp(type, "GET") == 0) && strncmp(file, "cgi-bin/", 8) != 0) { 
		FILE *openf = fopen(file, "r"); 
		
		if (openf == NULL) {
			char *response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 0\r\n\r\n";
	
			send(client_socket, response, strlen(response), 0); 
			close(client_socket); 
			return NULL; 
		}
		fclose(openf); 
	}

	if ((strcmp(type, "HEAD") == 0 || strcmp(type, "GET") == 0) && strncmp(file, "cgi-bin/", 8) != 0) {
		snprintf(header, 1024, 
			"HTTP/1.1 200 OK\r\n" 
			"Content-Type: text/html\r\n"
			"Content-Length: %ld\r\n\r\n", 
			file_stat.st_size);

		send(client_socket, header, strlen(header), 0); //common header
	}

	if (strcmp(type, "HEAD") == 0) {
		close(client_socket); 
		return NULL; 

	} else if (strcmp(type, "GET") == 0) {
		if (strncmp(file, "cgi-bin/", 8) == 0) {
			cgi_bin(client_socket, file + 7); 
			return NULL; 

		} else {
			get_req(client_socket, file); 
			return NULL;
		}
	} else {
		char *response = "HTTP/1.1 501 Not Implemented\r\nContent Type: text/html\r\n\r\n";

		send(client_socket, response, strlen(response), 0);
		close(client_socket); 
		return NULL; 
	}
}


int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("command line args issue\n"); 
		return 1; 
	}
	int port_num = atoi(argv[1]); 
	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (sock_fd < 0) {
		printf("sockfd issue\n");
		return 1; 
	}
	struct sockaddr_in sa; 

	sa.sin_family = AF_INET;
	sa.sin_port = htons(port_num);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sock_fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		close(sock_fd); 
		printf("bind issue"); 
		return 1; 
	}

	listen(sock_fd, 5); //5 reps amount of clients that can listen at any time
	printf("listening on port %i\n", port_num);

	for (;;) {
		struct sockaddr_in client_sa;

		socklen_t client_sa_len;
		char client_addr[INET_ADDRSTRLEN];
		int client_socket = accept(sock_fd, (struct sockaddr *) &client_sa,
		&client_sa_len);
		
		int *client_socketp = (int *)malloc(sizeof(int));
		
		*client_socketp = client_socket; 

		inet_ntop(AF_INET, &(client_sa.sin_addr), client_addr, INET_ADDRSTRLEN); 
		printf("client_socket: %d (%s:%d)\n", *client_socketp, client_addr, ntohs(client_sa.sin_port)); 
	
		pthread_t client_thread; 
		int result = pthread_create(&client_thread, NULL, handle_client_request, client_socketp); 

		if (result != 0) {
			printf("HTTP/1.1 500\r\nPthread Create Error\r\nContent Type: text/html\r\n\r\n");  
			close(sock_fd); 
			return 1; 
		}

		result = pthread_detach(client_thread); 
		if (result != 0) {
			printf("HTTP/1.1 500\r\nPthread Detach Error\r\nContent Type: text/html\r\n\r\n"); 
			close(sock_fd); 
			return 1; 
		}
	}

	printf("closed port %i\n", port_num); 
	close(sock_fd); 
	return 0; 
}