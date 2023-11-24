#include"proxy_server.hpp"

int main() {
    int Connect;
    int Server = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in ServerAddr;
    ServerAddr.sin_family = AF_INET;
    ServerAddr.sin_port = htons(8080);
    ServerAddr.sin_addr.s_addr = INADDR_ANY;
    bind(Server, (sockaddr*)&ServerAddr, sizeof(ServerAddr));
    listen(Server, SOMAXCONN);

    //create the thread to clean the cache
    //implement in cache.hpp
    std::thread cache_cleaner(delete_expired_cache);
    threads.emplace_back(&cache_cleaner);

    //accept connections
    while (true) {
        sockaddr_in ClientAddr;
        socklen_t client_len = sizeof(ClientAddr);
        Connect = accept(Server, (sockaddr*)&ClientAddr, &client_len);
        threads.emplace_back(new std::thread((ClientHandler()), Connect,ClientAddr));
    }

}



// try(require(&lock)){

// }catch()