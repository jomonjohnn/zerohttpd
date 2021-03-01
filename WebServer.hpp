
#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "config.hpp"
#include "IOUring.hpp"
#include "Network.hpp"


class WebServer{

public:
	WebServer(const uint16_t port) :
		port_m(port),
		socket_m(DEFAULT_SERVER_PORT),
		ring_m(QUEUE_DEPTH),
		quit_m(false)
	{
		server_socket_m = socket_m.Listen();

		/* Setup signal handling via signalfd */
		sigset_t mask;
		sigemptyset(&mask);
		sigaddset(&mask, SIGINT);

		sigprocmask(SIG_BLOCK, &mask, NULL);
		int sfd = signalfd(-1, &mask, SFD_NONBLOCK);

		/* Using epoll due to current issues of signalfd with io_ring */
		epfd = epoll_create(1);
		struct epoll_event ev;
		ev.data.fd = sfd;
		ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
		epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev);

		ring_m.setupSignalHandling(epfd);
	}

	void Run(void)
	{
		struct io_uring_cqe *cqe;
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);

		std::cout << "Serving on port : " << port_m << "\n";

		ring_m.doAccept(server_socket_m, &client_addr, &client_addr_len);

		while (!quit_m) {

			ring_m.doWait(&cqe);
			struct request *req = (struct request *) cqe->user_data;
			if (cqe->res < 0) {
				fprintf(stderr, "Async request failed: %s for event: %d\n",
						strerror(-cqe->res), Utility::to_underlying(req->event_type));
				exit(1);
			}

			switch (req->event_type) {
				case EventType::Accept:
					ring_m.doAccept(server_socket_m, &client_addr, &client_addr_len);
					ring_m.doRead(cqe->res);
					free(req);
					break;
				case EventType::Read:
					if (!cqe->res) {
						std::cerr << "Empty request!\n";
						break;
					}
					handle_client_request(req);
					free(req->iov[0].iov_base);
					free(req);
					break;
				case EventType::Write:
					for (int i = 0; i < req->iovec_count; i++) {
						free(req->iov[i].iov_base);
					}
					close(req->client_socket);
					free(req);
					break;
				case EventType::Quit:
					quit_m = true;
					free(req);
					break;
			}
			
			ring_m.markSeen(cqe);

			if(quit_m){
				ring_m.doStop();
				close(epfd);
			}
		}
	}

private:
	uint16_t port_m;
	Socket socket_m;
	IOUring ring_m;

	int server_socket_m;
	int epfd;
	bool quit_m;

private:

	void handle_get_method(char *path, int client_socket) {

		char final_path[1024];

		/*
		 If a path ends in a trailing slash, the client probably wants the index
		 file inside of that directory.
		 */
		if (path[strlen(path) - 1] == '/') {
			strcpy(final_path, "public");
			strcat(final_path, path);
			strcat(final_path, "index.html");
		}
		else {
			strcpy(final_path, "public");
			strcat(final_path, path);
		}

		sendFile(final_path, client_socket);
	}


	/*
	 * This function looks at method used and calls the appropriate handler function.
	 * Since we only implement GET and POST methods, it calls handle_unimplemented_method()
	 * in case both these don't match. This sends an error to the client.
	 * */

	void handle_http_method(char *method_buffer, int client_socket) {
		char *method, *path, *saveptr;

		method = strtok_r(method_buffer, " ", &saveptr);
		Utility::strtolower(method);
		path = strtok_r(NULL, " ", &saveptr);

		if (strcmp(method, "get") == 0) {
			handle_get_method(path, client_socket);
		}
		else {
			sendString(unimplemented_content, client_socket);
		}
	}

	int sendString(const char *str, int client_socket)
	{
		struct request *req = static_cast<struct request *>(Utility::zh_malloc(sizeof(*req) + sizeof(struct iovec)));
		unsigned long slen = strlen(str);
		req->iovec_count = 1;
		req->client_socket = client_socket;
		req->iov[0].iov_base = Utility::zh_malloc(slen);
		req->iov[0].iov_len = slen;
		memcpy(req->iov[0].iov_base, str, slen);
		ring_m.doWrite(req);

		return 0;
	}

	int sendFile(char *final_path, int client_socket)
	{

		/* The stat() system call will give you information about the file
		 * like type (regular file, directory, etc), size, etc. */
		struct stat path_stat;
		if (stat(final_path, &path_stat) == -1) {
			printf("404 Not Found: (%s)\n", final_path);
			sendString(http_404_content, client_socket);
		}
		else {
			/* Check if this is a normal/regular file and not a directory or something else */
			if (S_ISREG(path_stat.st_mode)) {
				struct request *req = static_cast<struct request *>(Utility::zh_malloc(sizeof(*req) + (sizeof(struct iovec) * 6)));
				req->iovec_count = 6;
				req->client_socket = client_socket;
				send_headers(final_path, path_stat.st_size, req->iov);
				copy_file_contents(final_path, path_stat.st_size, &req->iov[5]);
				printf("200 %s %ld bytes\n", final_path, path_stat.st_size);
				ring_m.doWrite(req);
			}
			else {
				sendString(http_404_content, client_socket);
				printf("404 Not Found: %s\n", final_path);
			}
		}

		return 0;
	}

	int get_line(const char *src, char *dest, int dest_sz) {
		for (int i = 0; i < dest_sz; i++) {
			dest[i] = src[i];
			if (src[i] == '\r' && src[i+1] == '\n') {
				dest[i] = '\0';
				return 0;
			}
		}
		return 1;
	}

	int handle_client_request(struct request *req) {
		char http_request[1024];
		/* Get the first line, which will be the request */
		if(get_line(static_cast<const char*>(req->iov[0].iov_base), http_request, sizeof(http_request))) {
			fprintf(stderr, "Malformed request\n");
			exit(1);
		}
		handle_http_method(http_request, req->client_socket);
		return 0;
	}

	/*
	 * Once a static file is identified to be served, this function is used to read the file
	 * and write it over the client socket using Linux's sendfile() system call. This saves us
	 * the hassle of transferring file buffers from kernel to user space and back.
	 * */

	void copy_file_contents(char *file_path, off_t file_size, struct iovec *iov) {
		int fd;

		char *buf = static_cast<char *>(Utility::zh_malloc(file_size));
		fd = open(file_path, O_RDONLY);
		if (fd < 0)
			Utility::FatalError("open");

		/* We should really check for short reads here */
		int ret = read(fd, buf, file_size);
		if (ret < file_size) {
			fprintf(stderr, "Encountered a short read.\n");
		}
		close(fd);

		iov->iov_base = buf;
		iov->iov_len = file_size;
	}


	/*
	 * Simple function to get the file extension of the file that we are about to serve.
	 * */

	const char *get_filename_ext(const char *filename) {
		const char *dot = strrchr(filename, '.');
		if (!dot || dot == filename)
			return "";
		return dot + 1;
	}

	/*
	 * Sends the HTTP 200 OK header, the server string, for a few types of files, it can also
	 * send the content type based on the file extension. It also sends the content length
	 * header. Finally it send a '\r\n' in a line by itself signalling the end of headers
	 * and the beginning of any content.
	 * */

	void send_headers(const char *path, off_t len, struct iovec *iov) {

		char small_case_path[1024];
		char send_buffer[1024];
		strcpy(small_case_path, path);
		Utility::strtolower(small_case_path);

		char const *str = "HTTP/1.0 200 OK\r\n";
		unsigned long slen = strlen(str);
		iov[0].iov_base = Utility::zh_malloc(slen);
		iov[0].iov_len = slen;
		memcpy(iov[0].iov_base, str, slen);

		slen = strlen(SERVER_STRING);
		iov[1].iov_base = Utility::zh_malloc(slen);
		iov[1].iov_len = slen;
		memcpy(iov[1].iov_base, SERVER_STRING, slen);

		/*
		 * Check the file extension for certain common types of files
		 * on web pages and send the appropriate content-type header.
		 * Since extensions can be mixed case like JPG, jpg or Jpg,
		 * we turn the extension into lower case before checking.
		 * */
		const char *file_ext = get_filename_ext(small_case_path);
		if (strcmp("jpg", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: image/jpeg\r\n");
		if (strcmp("jpeg", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: image/jpeg\r\n");
		if (strcmp("png", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: image/png\r\n");
		if (strcmp("gif", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: image/gif\r\n");
		if (strcmp("htm", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: text/html\r\n");
		if (strcmp("html", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: text/html\r\n");
		if (strcmp("js", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: application/javascript\r\n");
		if (strcmp("css", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: text/css\r\n");
		if (strcmp("txt", file_ext) == 0)
			strcpy(send_buffer, "Content-Type: text/plain\r\n");
		slen = strlen(send_buffer);
		iov[2].iov_base = Utility::zh_malloc(slen);
		iov[2].iov_len = slen;
		memcpy(iov[2].iov_base, send_buffer, slen);

		/* Send the content-length header, which is the file size in this case. */
		sprintf(send_buffer, "content-length: %ld\r\n", len);
		slen = strlen(send_buffer);
		iov[3].iov_base = Utility::zh_malloc(slen);
		iov[3].iov_len = slen;
		memcpy(iov[3].iov_base, send_buffer, slen);

		/*
		 * When the browser sees a '\r\n' sequence in a line on its own,
		 * it understands there are no more headers. Content may follow.
		 * */
		strcpy(send_buffer, "\r\n");
		slen = strlen(send_buffer);
		iov[4].iov_base = Utility::zh_malloc(slen);
		iov[4].iov_len = slen;
		memcpy(iov[4].iov_base, send_buffer, slen);
	}
};
