/*
 * zerohttpd++
 * 
 * WIP C++ Port of io_uring based Webserver capable of serving static files 
 * Original Implementation : https://github.com/shuveb/io_uring-by-example/blob/master/04_cp_liburing/main.c
 * 
 */

#include "config.hpp"
#include "WebServer.hpp"

int main(int argc, char** argv)
{
	int ret = EXIT_SUCCESS;

	try{
		WebServer server(DEFAULT_SERVER_PORT);
		server.Run();
	}catch(std::exception& exp){
		std::cerr << exp.what() << std::endl;
		ret = EXIT_FAILURE;
	}

	return ret;
}
