zerohttpd++: main.cpp
	g++ --std=c++17 -Wall -o $@ $< -luring

all: zerohttpd++

.PHONY: clean

clean:
	rm -f zerohttpd++
