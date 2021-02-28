
#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <cstdint>

#include "Utility.hpp"

class Socket
{

public:
	Socket(const uint16_t inport) :
		port(inport),
		listening (false)
	{}

	int Listen(void)
	{
	    sock = socket(PF_INET, SOCK_STREAM, 0);
	    if (sock == -1)
	        Utility::FatalError("socket()");

	    int enable = 1;
	    if (setsockopt(sock,
	                   SOL_SOCKET, SO_REUSEADDR,
	                   &enable, sizeof(int)) < 0)
	        Utility::FatalError("setsockopt(SO_REUSEADDR)");


	    memset(&srv_addr, 0, sizeof(srv_addr));
	    srv_addr.sin_family = AF_INET;
	    srv_addr.sin_port = htons(port);
	    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	    /* We bind to a port and turn this socket into a listening
	     * socket.
	     * */
	    if (bind(sock,
	             (const struct sockaddr *)&srv_addr,
	             sizeof(srv_addr)) < 0)
	        Utility::FatalError("bind()");

	    if (listen(sock, 10) < 0)
	        Utility::FatalError("listen()");

	    listening = true;
	    return (sock);
	}

	int getFd(void){
		int ret = -1;
		
		if(listening){
			ret = sock;
		}
		
		return (ret);
	}

private:
	uint16_t port;
    int sock;
    struct sockaddr_in srv_addr;
    bool listening;
};
