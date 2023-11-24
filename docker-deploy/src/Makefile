CC=g++ -pthread -std=c++17

all: server client proxy

proxy: proxy_server.cpp
	$(CC) -o proxy proxy_server.cpp

.PHONY:
	clean

clean:
	rm -rf *.o proxy

# g++ -std=c++17 -I ./boost