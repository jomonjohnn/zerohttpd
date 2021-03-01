
#pragma once

#include <cerrno>
#include <stdexcept>
#include <cstring>

#include <sys/poll.h>
#include <liburing.h>

#include "types.hpp"
#include "Utility.hpp"

class IOUring{

public:
	IOUring(unsigned int entries, unsigned int flags = 0) :
		entries_m(entries),
		last_accept_req(nullptr)
	{
		int ret = io_uring_queue_init(entries_m, &ring_m, flags);
		if( ret != 0){
			std::runtime_error(std::strerror(errno));
		}
	}

	~IOUring()
	{
		if(last_accept_req != nullptr){
			free(last_accept_req);
		}
	}

	int doAccept(int server_socket, struct sockaddr_in *client_addr,
					   socklen_t *client_addr_len)
	{
		struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_m);
		io_uring_prep_accept(sqe, server_socket, (struct sockaddr *) client_addr,
							 client_addr_len, 0);
		struct request *req = static_cast<struct request *>(malloc(sizeof(*req)));
		last_accept_req = req;
		req->event_type = EventType::Accept;
		io_uring_sqe_set_data(sqe, req);
		io_uring_submit(&ring_m);
		return 0;
	}

	int doRead(int client_socket)
	{
		struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_m);
		struct request *req = static_cast<struct request *>(malloc(sizeof(*req) + sizeof(struct iovec)));
		req->iov[0].iov_base = malloc(READ_SZ);
		req->iov[0].iov_len = READ_SZ;
		req->event_type = EventType::Read;
		req->client_socket = client_socket;
		memset(req->iov[0].iov_base, 0, READ_SZ);
		/* Linux kernel 5.5 has support for readv, but not for recv() or read() */
		io_uring_prep_readv(sqe, client_socket, &req->iov[0], 1, 0);
		io_uring_sqe_set_data(sqe, req);
		io_uring_submit(&ring_m);
		return 0;
	}

	int doWrite(struct request *req)
	{
		struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_m);
		req->event_type = EventType::Write;
		io_uring_prep_writev(sqe, req->client_socket, req->iov, req->iovec_count, 0);
		io_uring_sqe_set_data(sqe, req);
		io_uring_submit(&ring_m);
		return 0;
	}

	int doWait(struct io_uring_cqe** cqe)
	{
		int ret = io_uring_wait_cqe(&ring_m, cqe);
		if (ret < 0){
			Utility::FatalError("io_uring_wait_cqe");
		}
		return ret;
	}

	void markSeen(struct io_uring_cqe* cqe)
	{
		/* Mark this request as processed */
		io_uring_cqe_seen(&ring_m, cqe);
	}

	void doStop(void)
	{
		io_uring_queue_exit(&ring_m);	
	}

	void setupSignalHandling(int epfd)
	{
		struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_m);
		io_uring_prep_poll_add(sqe, epfd, POLLIN);

		struct request *req = static_cast<struct request *>(malloc(sizeof(*req)));
		req->event_type = EventType::Quit;
		io_uring_sqe_set_data(sqe, req);
		io_uring_submit(&ring_m);
	}

private:
	unsigned int entries_m;
	struct io_uring ring_m;
	struct request *last_accept_req;
};
