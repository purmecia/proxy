#pragma once
#ifndef COMMON_HPP
#define COMMON_HPP

//exception handle
#include <exception>
#include <stdexcept>

#define PRINTID(ID)  "ID " <<ID<<": "
#define PRINTIDERR(ID)  "ID " <<ID<<": [ERROR] "
#define PRINTIDWARN(ID)  "ID " <<ID<<": [WARNING] "
#define PRINTIDNOTE(ID)  "ID " <<ID<<": [NOTE] "

#define LOG_OUTPUT(msg)  \
{\
    std::ostringstream _os;\
    _os << msg\
    std::cout << _os.str();\
}

//create a server connection exception
class ServerConnectionException : public std::runtime_error {
public:
    ServerConnectionException(): std::runtime_error("Server Connection Exception") {}
    int get_error_code() {
        return error_code;
    }
private:
    int error_code=1;
};

//create a client connection exception
class ClientConnectionException : public std::runtime_error {
public:
    ClientConnectionException(): std::runtime_error("Client Connection Exception") {}
    int get_error_code() {
        return error_code;
    }
private:
    int error_code=2;
};


//create a exception handle for the server connection
//if server connection exception, return 502 response
//If client connection exception, return 400 response
void exception_handle(int Connect, int error_code) {
    std::string response;
    if (error_code == 1) {
        response = "HTTP/1.1 502 Bad Gateway\r\n\r\n";

    }
    else if (error_code == 2) {
        response = "HTTP/1.1 400 Bad Request\r\n\r\n";
    }
    send(Connect, response.c_str(), response.size(), 0);
    // close(Connect);
    // pthread_exit(NULL);
}



#endif // !COMMON_HPP
