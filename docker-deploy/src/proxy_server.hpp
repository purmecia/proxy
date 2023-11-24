#pragma once

#ifndef PROXY_SERVER_HPP
#define PROXY_SERVER_HPP

#include<iostream>
#include<memory>
#include<thread>
#include<sys/socket.h> 
#include<arpa/inet.h> 
#include<unistd.h>
#include<netdb.h>
#include"http_request_parser.hpp"
#include"http_response_parser.hpp"
#include"common.hpp"
#include"cache.hpp"



std::vector<std::unique_ptr<std::thread> > threads;

class ClientHandler {
public:
    ClientHandler() {}
    void operator()(int Connect, sockaddr_in ClientAddr) {
        std::vector<char> buf(10000);
        int first_requst_byte = recv(Connect, buf.data(), buf.size(), 0);
        
        if(first_requst_byte<=0){
            exception_handle(Connect, 2);
            close(Connect);
            return;
        }

        auto first_request=Request();
        try{
            first_request=Parsing_HTTP_request(buf.data(),first_requst_byte,time(NULL),Connect);
            if(first_request.error_code!=NO_ERROR){
                throw ClientConnectionException();
            }
        }
        catch(...){
            exception_handle(Connect, 2);
            LOG_OUTPUT(PRINTIDERR(Connect)<<"Invalid request"<<std::endl;)
            close(Connect);
            return;
        }
        //whether the request is valid


        first_request.client_addr=ClientAddr;
        LOG_OUTPUT(PRINTID(Connect)"\""<<first_request.method<<" "<<first_request.url<<" "<<first_request.version<<"\" from "<<inet_ntoa(ClientAddr.sin_addr)<<" @ "<<asctime(gmtime(&first_request.req_time));)
        
        if(first_request.method=="CONNECT"){
            // Print_request(first_request);
            std::string response = "HTTP/1.1 200 OK\r\n\r\n";
            send(Connect, response.c_str(), response.size(), 0);
            //create a new socket to connect to the address in first_request
            sockaddr_in ServerAddr;
            int Connect2 = link_server(Connect, first_request, ServerAddr);
            //use select to read from both sockets and write to both sockets
            if(Connect2==-1)
                return;
            Connect_transfer(Connect, Connect2);
        }
        else if (first_request.method == "GET")
        {   
            // std::cout<<"Bytes: "<<bytes<<std::endl;
            // Print_request(first_request);
            // std::string raw_str(buf.data(), bytes);
            // std::cout<<"Received request:"<<raw_str<<std::endl;
            try{
                std::lock_guard<std::mutex> lock(cache.cache_mutex[cache_hash(first_request.url)]);
                
                //case 1: not in cache
                if(cache.IS_not_in_cache(Connect, first_request.url)){
                    LOG_OUTPUT(PRINTID(Connect) "not in cache"<<std::endl;)
                    int err = GET_transfer_not_in_cache(Connect, first_request,buf,first_requst_byte);
                    if(err==-1)
                        close(Connect);
                        return;
                }
                else{
                    auto cache_resp = cache.find_cache(Connect, first_request.url);
                    //case 2: in chache, valid (fresh)
                    if(Is_fresh_req_resp(first_request, cache_resp)){
                        LOG_OUTPUT(PRINTID(Connect) "in cache, valid"<<std::endl;)
                        //send the response to the client
                        std::string to_send = Format_response(cache_resp);
                        send(Connect, to_send.c_str(), to_send.size(), 0);
                        LOG_OUTPUT(PRINTID(Connect)"Responding \""<<cache_resp.version<<" "<<cache_resp.status_code<<" "<<cache_resp.status_name<<"\" "<<std::endl;)
                        close(Connect);
                        return;
                    }

                    //case 3: in cahce, but require re-validation
                    else{
                        //case 3.1 : in cache, but expired
                        if(!Is_need_revalidation_req_resp(first_request, cache_resp)){
                            LOG_OUTPUT(PRINTID(Connect) "in cache, but expired at "<<asctime(gmtime(&cache_resp.cache_control.expiration_time));)
                        }
                        //case 3.2 : in cache, but need validation
                        LOG_OUTPUT(PRINTID(Connect) "in cache, but need re-validation"<<std::endl;)
                        int err = GET_transfer_need_revalidation(Connect, first_request, cache_resp, buf, first_requst_byte);
                        if(err==-1)
                            return;
                    }
                }
                    

            }catch(...){
                LOG_OUTPUT(PRINTIDERR(Connect) "Error in dealing with get"<<std::endl;)
                LOG_OUTPUT(PRINTIDERR(Connect) "Error request: " << std::endl;)
                Print_request(first_request);
                exception_handle(Connect, 1);
                close(Connect);
                // pthread_exit(NULL);
                return;
            }



        }
        else if (first_request.method == "POST")
        {   
            sockaddr_in ServerAddr;
            int Connect2 = link_server(Connect, first_request, ServerAddr);
            //send the request to the server
            
            send(Connect2, buf.data(), first_requst_byte, 0);
            //read the response from the server
            LOG_OUTPUT(PRINTID(Connect)"Requesting \""<<first_request.method<<" "<<first_request.url<<" "<<first_request.version<<"\" from "<<inet_ntoa(ServerAddr.sin_addr)<<std::endl;)

            std::vector<char> buf_server(10000);
            int first_response_byte = recv(Connect2, buf_server.data(), buf_server.size(), 0);
            if(first_response_byte<=0){
                close(Connect2);
                close(Connect);
                LOG_OUTPUT(PRINTID(Connect)"Connection closed"<<std::endl;)
                return;
            }
            auto first_response=Response();
            try{
                first_response=Parsing_HTTP_response(buf_server.data(),first_response_byte,time(NULL),Connect);
                LOG_OUTPUT(PRINTID(Connect)"Received \""<<first_response.version<<" "<<first_response.status_code<<" "<<first_response.status_name<<"\" from "<<inet_ntoa(ServerAddr.sin_addr)<<std::endl;)
                if(first_response.error_code!=OK){
                    throw ServerConnectionException();
                }
            }
            catch(...){
                exception_handle(Connect, 1);
                close(Connect2);
                close(Connect);
                return;
            }
            
            // Print_response(first_response);
            

            send(Connect, buf_server.data(), first_response_byte, 0);
            LOG_OUTPUT(PRINTID(Connect)"Responding \""<<first_response.version<<" "<<first_response.status_code<<" "<<first_response.status_name<<std::endl;)

            // int now_bytes=first_response_byte;
            // int total_bytes=first_response.content_length;

            recv_from_server(Connect, Connect2, first_response,first_response_byte);

            //close the connection
            close(Connect2);
            close(Connect);
            LOG_OUTPUT(PRINTID(Connect)"Connection closed"<<std::endl;)
            return;
        }
    }
private:
    //create a new socket to connect the server
    //used in CONNECT and GET/POST method
    int link_server(int Connect_id, Request& req,sockaddr_in& ServerAddr){
        int Connect_server = socket(AF_INET, SOCK_STREAM, 0);
        //transfer the host to ip address
        struct hostent * remoteHost;
        try{
            if( (remoteHost = gethostbyname(req.host.c_str())) == 0 ){
                LOG_OUTPUT(PRINTIDERR(Connect_id) <<"Error: cannot resolve hostname "<<req.host<<std::endl;)
                LOG_OUTPUT(PRINTIDERR(Connect_id) <<"Error req: "<<req.raw_request<<std::endl;)
                throw ServerConnectionException();
            }
        }catch(...){
            exception_handle(Connect_id, 1);
            // pthread_exit(NULL);
            return -1;
        }

        ServerAddr.sin_family = AF_INET;
        ServerAddr.sin_port = htons(req.port);
        ServerAddr.sin_addr.s_addr = inet_addr(inet_ntoa(*(struct in_addr *)*remoteHost->h_addr_list));
        try{
            if(connect(Connect_server, (sockaddr*)&ServerAddr, sizeof(ServerAddr))<0){
                LOG_OUTPUT(PRINTIDERR(Connect_id) <<"Error: cannot connect to "<<req.host<<std::endl;)
                LOG_OUTPUT(PRINTIDERR(Connect_id) <<"Error req: "<<req.raw_request<<std::endl;)

                throw ServerConnectionException();
            }
        }catch(ServerConnectionException& e){
            exception_handle(Connect_id, e.get_error_code());
            // pthread_exit(NULL);
            return -1;
        }
        // } else {
        //     PRINTID std::cout<<"Tunnel built"<<std::endl;
        // }

        return Connect_server;
    }

    // use select to read from both sockets and write to both sockets
    // used in CONNECT method
    void Connect_transfer(int& Connect1, int& Connect2){
        fd_set readfds;
        while(true){
            FD_ZERO(&readfds);
            FD_SET(Connect1, &readfds);
            FD_SET(Connect2, &readfds);
            int maxfd = std::max(Connect1, Connect2);
            select(maxfd+1, &readfds, NULL, NULL, NULL);
            int bytes;
            std::vector<char> buf(10000);
            if(FD_ISSET(Connect1, &readfds)){
                // PRINTID std::cout<<"read from client"<<std::endl;
                bytes = recv(Connect1, buf.data(), buf.size(), 0);
                if(bytes>0)
                    send(Connect2, buf.data(), bytes, 0);
                else{
                    LOG_OUTPUT(PRINTID(Connect1)<<"Tunnel closed"<<std::endl;)
                    return;
                }
            }
            if(FD_ISSET(Connect2, &readfds)){
                // PRINTID std::cout<<"read from server"<<std::endl;
                std::vector<char> buf_server(10000);
                bytes = recv(Connect2, buf_server.data(), buf_server.size(), 0);
                if(bytes>0)
                    send(Connect1, buf_server.data(), bytes, 0);
                else{
                    LOG_OUTPUT(PRINTID(Connect1)<<"Tunnel closed"<<std::endl;)
                    return;
                }
            }
        }
    }

    // for GET method
    // for request case 1 : not in cache 
    // receive the response from server and send it to client
    int GET_transfer_not_in_cache(int& Connect, Request& req, std::vector<char>& received_buf, int& received_bytes){
        //create a new socket to connect the server 
        // Print_request(req);
        sockaddr_in ServerAddr;
        int Connect2 = link_server(Connect, req,ServerAddr);
        //send the request to the server
        if(Connect2==-1)
            return -1;

        send(Connect2, received_buf.data(), received_bytes, 0);
        LOG_OUTPUT(PRINTID(Connect)"Requesting \""<<req.method<<" "<<req.url<<" "<<req.version<<"\" from "<<inet_ntoa(ServerAddr.sin_addr)<<std::endl;)

        //read the response from the server
        std::vector<char> buf_server(10000);
        int first_response_byte = recv(Connect2, buf_server.data(), buf_server.size(), 0);
        if(first_response_byte<=0){
            close(Connect2);
            close(Connect);
            LOG_OUTPUT(PRINTID(Connect)"Connection closed"<<std::endl;)
            // pthread_exit(NULL);
            return -1;
        }

        auto first_response = Response();
        try{
            first_response=Parsing_HTTP_response(buf_server.data(),first_response_byte,time(NULL),Connect);
            // Print_response(first_response);

            if(first_response.error_code!=OK){
                LOG_OUTPUT(PRINTIDERR(Connect)<<"Response error code: "<<first_response.error_code<<std::endl;)
                throw ServerConnectionException();
            }
        }catch(...){
            exception_handle(Connect, 1);
            close(Connect2);
            close(Connect);
            // pthread_exit(NULL);
            return -1;
        }
        // Print_response(first_response);

        LOG_OUTPUT(PRINTID(Connect)"Received \""<<first_response.version<<" "<<first_response.status_code<<" "<<first_response.status_name<<"\" from "<<inet_ntoa(ServerAddr.sin_addr)<<std::endl;)

        send(Connect, buf_server.data(), first_response_byte, 0);
        LOG_OUTPUT(PRINTID(Connect)"Responding \""<<first_response.version<<" "<<first_response.status_code<<" "<<first_response.status_name<<std::endl;)

        // If reveiced response is not 200 OK, keep receiving without caching
        if(first_response.status_code != 200){
            recv_from_server(Connect,Connect2,first_response,first_response_byte);
        }
        // If received response is 200 OK, keep receiving and caching
        else{
            //Case1: If the response it not cacheble, then keep receiving and sending
            //Output not cacheble because REASON
            if(first_response.cache_control.Is_not_cacheable()){
                LOG_OUTPUT(PRINTID(Connect)"not cacheable because "<<first_response.cache_control.reason<<std::endl;)
                recv_from_server(Connect,Connect2,first_response,first_response_byte);

            }else if(req.cache_control.Is_not_cacheable()){
                // LOG_OUTPUT("Not con"<<std::endl;)
                LOG_OUTPUT(PRINTID(Connect)"not cacheable because "<<req.cache_control.reason<<std::endl;)
                recv_from_server(Connect,Connect2,first_response,first_response_byte);                
            }
            // The response is cacheble
            else{
                //Case2: If the response is cacheable but need re-validation
                //Output cacheable, expires at time, need re-validation


                //keep receiving caching and sending
                recv_from_server(Connect,Connect2,first_response,first_response_byte);
                
                //cache the response
                cache.insert_cache(Connect,req.url,first_response);
                
                // PRINTID(Connect) "[DEBUG] Need revalidation: " << first_response.cache_control.Is_need_revalidation() << std::endl;

                if(first_response.cache_control.Is_need_revalidation()||\
                    req.cache_control.Is_need_revalidation()){
                    LOG_OUTPUT(PRINTID(Connect)"cached, but requires re-validation"<<std::endl;)
                } 
                //Case3: If the response is cacheble and will expire
                else{
                    LOG_OUTPUT(PRINTID(Connect)"cached, expires at "<<asctime(gmtime(&first_response.cache_control.expiration_time));)
                }   
            }
        }
    

        //close the connection
        close(Connect2);
        close(Connect);
        LOG_OUTPUT(PRINTID(Connect)"Connection closed"<<std::endl;)
        return 0;
    }   

    // for GET method
    // for request case 3 : in cache but need re-validation
    // send revalidation request to server and receive the response
    int GET_transfer_need_revalidation(int& Connect, Request& req, Response& res, std::vector<char>& received_buf, int& received_bytes){
        //Whether we have info to revalidate
        if(req.cache_control.if_modified_since == "" && req.cache_control.if_none_match == ""\
            &&res.cache_control.last_modified == "" && res.cache_control.Etag == ""){
            LOG_OUTPUT(PRINTIDERR(Connect)"cannot revalidate because no enough info"<<std::endl;)
            int err = GET_transfer_not_in_cache(Connect,req,received_buf,received_bytes);
            if(err==-1)
                return -1;
            else{
                return 0;
            }
        }

        sockaddr_in ServerAddr;
        int Connect2 = link_server(Connect, req,ServerAddr);

        //failed to connect to server
        if(Connect2==-1)
            return -1;
        //build a revalidation request  
        std::string revalidation_request = "GET "+req.url+" HTTP/1.1\r\n";
        revalidation_request += "Host: "+req.host+"\r\n";
        
        if(req.cache_control.if_modified_since != ""){
            revalidation_request += "If-Modified-Since: "+req.cache_control.if_modified_since+"\r\n";
        }else if(res.cache_control.last_modified != ""){
            revalidation_request += "If-Modified-Since: "+res.cache_control.last_modified+"\r\n";
        }

        if(req.cache_control.if_none_match != ""){
            revalidation_request += "If-None-Match: "+req.cache_control.if_none_match+"\r\n";
        }else if(res.cache_control.Etag != ""){
            revalidation_request += "If-None-Match: "+res.cache_control.Etag+"\r\n";
        }

        revalidation_request += "\r\n";

        //send the revalidation request to server
        // std::cout<<"[DEBUG] revalidation_request: "<<std::endl<<revalidation_request<<std::endl;

        send(Connect2, revalidation_request.c_str(), revalidation_request.size(), 0);
        LOG_OUTPUT(PRINTID(Connect)"Requesting \""<<req.method<<" "<<req.url<<" "<<req.version<<"\" from "<<inet_ntoa(ServerAddr.sin_addr)<<std::endl;)

        //read the response from the server
        std::vector<char> buf_server(10000);
        int revalidation_response_byte = recv(Connect2, buf_server.data(), buf_server.size(), 0);
        if(revalidation_response_byte<=0){
            close(Connect2);
            close(Connect);
            LOG_OUTPUT(PRINTID(Connect)"Connection closed"<<std::endl;)
            //pthread_exit(NULL);
            return -1;
        }

        auto revalidation_response=Response();
        try{
            revalidation_response=Parsing_HTTP_response(buf_server.data(),revalidation_response_byte,time(NULL),Connect);
            // std::cout<<"[DEBUG] revalidation_response: "<<std::endl;
            // Print_response(revalidation_response);
            if(revalidation_response.error_code!=OK){
                throw ServerConnectionException();
            }
        }
        catch(ServerConnectionException& e){
            exception_handle(Connect,e.get_error_code());
            close(Connect2);
            close(Connect);
            LOG_OUTPUT(PRINTID(Connect)"Connection closed"<<std::endl;)
            return -1;
        }
        // Print_response(revalidation_response);

        LOG_OUTPUT(PRINTID(Connect)"Received \""<<revalidation_response.version<<" "<<revalidation_response.status_code<<" "<<revalidation_response.status_name<<"\" from "<<inet_ntoa(ServerAddr.sin_addr)<<std::endl;)

        //If the response is 304 Not Modified
        if(revalidation_response.status_code == 304){
            //update the cached response expiration time

            Update_response(res, revalidation_response);
            //send the cached response to client
            std::string cached_response = Format_response(res);
            send(Connect, cached_response.c_str(), cached_response.size(), 0);
            LOG_OUTPUT(PRINTID(Connect)"Responding \""<<res.version<<" "<<res.status_code<<" "<<res.status_name<<std::endl;)
        }
        //deal with others
        else {
            //send back the response to client
            send(Connect, buf_server.data(), revalidation_response_byte, 0);
            LOG_OUTPUT(PRINTID(Connect)"Responding \""<<revalidation_response.version<<" "<<revalidation_response.status_code<<" "<<revalidation_response.status_name<<std::endl;)
            // If reveiced response is not 200 OK, keep receiving without caching
            if(revalidation_response.status_code != 200){
                recv_from_server(Connect,Connect2,revalidation_response,revalidation_response_byte);
            }
            // If received response is 200 OK, keep receiving and update cache
            else{
                //Case1: If the response it not cacheble, then keep receiving and sending
                //Output not cacheble because REASON
                if(revalidation_response.cache_control.Is_not_cacheable()||req.cache_control.Is_not_cacheable()){
                    LOG_OUTPUT(PRINTID(Connect)"new response not cacheable because "<<revalidation_response.cache_control.reason<<std::endl;)
                    recv_from_server(Connect,Connect2,revalidation_response,revalidation_response_byte);
                }
                // The response is cacheble
                else{
                    //Case2: If the response is cacheable but need re-validation
                    //Output cacheable, expires at time, need re-validation
                    if(revalidation_response.cache_control.Is_need_revalidation()||\
                        req.cache_control.Is_need_revalidation()){
                        LOG_OUTPUT(PRINTID(Connect)"new response cached, but requires re-validation"<<std::endl;)
                    } 
                    //Case3: If the response is cacheble and will expire
                    else{
                        LOG_OUTPUT(PRINTID(Connect)"new response cached, expires at "<<asctime(gmtime(&revalidation_response.cache_control.expiration_time));)
                    }   

                    //keep receiving caching and sending
                    recv_from_server(Connect,Connect2,revalidation_response,revalidation_response_byte);
                    
                    //cache the response
                    cache.insert_cache(Connect,req.url,revalidation_response);

                }
            }
        }

        //close the connection
        close(Connect2);
        close(Connect);
        LOG_OUTPUT(PRINTIDNOTE(Connect)"Connection closed"<<std::endl;)
        return 0;
    }

        //recevie the response from server and send it to client
    int recv_from_server(int& Connect_client, int& Connect_server, Response& first_response, int total_bytes){

        //case 1: chunked encoding
        if(first_response.is_chunked==true){
            int total_chunk_size=0;
            std::string chunked_encoding=first_response.body;
            bool chunk_end=false;
            
            LOG_OUTPUT(PRINTIDNOTE(Connect_client)<<"Received chunked response"<<std::endl;)
            // PRINTIDNOTE(Connect_client)<<"The response is: "<<std::endl;
            // Print_response(first_response);
            while(true){
                if(chunk_end==true)
                    break;
                std::vector<char> buf(1000000);
                int bytes = recv(Connect_server, buf.data(), buf.size(), 0);
                if(bytes<=0)
                    break;
                else{
                    send(Connect_client, buf.data(), bytes, 0);
                    chunked_encoding.append(buf.data(),bytes);
                    while(!chunked_encoding.empty()){
                        std::string::size_type pos = chunked_encoding.find("\r\n");
                        if (pos == std::string::npos) {
                            break;
                        }
                        std::string sizeStr = chunked_encoding.substr(0, pos);
                        int now_size = std::stoi(sizeStr, nullptr, 16);
                        // LOG_OUTPUT(PRINTIDNOTE(Connect_client)<<"received chunk size: "<<now_size<<std::endl;)
                        // Check if we've reached the end of the chunked data
                        if (now_size == 0) {
                            chunk_end=true;
                            break;
                        }
                        // Extract the chunk data and store in a new string
                        chunked_encoding.erase(0, pos + 2);
                        first_response.body += chunked_encoding.substr(0, now_size);
                        chunked_encoding.erase(0, now_size + 2);
                        total_chunk_size+=now_size;
                    }
                    
                }
            }
            LOG_OUTPUT(PRINTIDNOTE(Connect_client)<<"receive chunk end "<<std::endl;)
            first_response.is_chunked=false;
            first_response.content_length=total_chunk_size;
            //aprend content_length to the response
            first_response.headers_map["Content-Length"]=std::to_string(total_chunk_size);
            //remove chunked encoding
            if(first_response.headers_map.find("Transfer-Encoding")!=first_response.headers_map.end()){
                if(first_response.headers_map["Transfer-Encoding"].find("chunked")!=std::string::npos)
                    first_response.headers_map["Transfer-Encoding"].erase(first_response.headers_map["Transfer-Encoding"].find("chunked"),7);
            }
            return 0;

        }


        //case 2: normal encoding
        else{
            total_bytes=first_response.body.size()+10;
            int max_length;
            if(first_response.content_length>0)
                max_length=first_response.content_length;
            else
                max_length=1000000000;

            if(max_length<=total_bytes)
                return 0;

            while(max_length>=total_bytes){
                std::vector<char> buf(10000);
                int bytes = recv(Connect_server, buf.data(), buf.size(), 0);
                first_response.body.append(buf.data(),bytes);
                if(bytes>0){
                    send(Connect_client, buf.data(), bytes, 0);
                    total_bytes+=bytes;
                }
                else
                    break;
            }

            // LOG_OUTPUT("[DEBUG]: Content_size:"<<first_response.content_length<<std::endl;)
            // LOG_OUTPUT("[DEBUG]: Total_bytes:"<<first_response.body.size() <<std::endl;)

            // try{
            //     if(Is_content_length_wrong(first_response))
            //         throw ServerConnectionException();
            // }
            // catch(ServerConnectionException& e){
            //     exception_handle(Connect_client,e.get_error_code());
            //     close(Connect_client);
            //     close(Connect_server);
            //     // pthread_exit(NULL);
            //     return -1;
            // }
        }
        return 0;
    }

    //the response is possibly too large to be sent in one time
    // void send_response_by_chunk(int& client_socket, Response& response){
    //     const std::size_t chunk_size = 1024; // set the chunk size
    //     std::size_t bytes_left = response.body.size();
    //     std::size_t bytes_sent = 0;
        
    //     // send the response headers with "Transfer-Encoding: chunked"
    //     std::stringstream headers;
    //     headers << "HTTP/1.1 200 OK\r\n";
    //     headers << "Content-Type: text/plain\r\n";
    //     headers << "Transfer-Encoding: chunked\r\n";
    //     headers << "\r\n";
    //     std::string headers_str = headers.str();
    //     send(client_socket, headers_str.c_str(), headers_str.size(), 0);

    //     // send the response body in chunks
    //     while (bytes_left > 0) {
    //         std::size_t bytes_to_send = std::min(bytes_left, chunk_size);
    //         std::stringstream chunk;
    //         chunk << std::hex << bytes_to_send << "\r\n"; // write the chunk size
    //         chunk << response.substr(bytes_sent, bytes_to_send) << "\r\n";
    //         std::string chunk_str = chunk.str();
    //         send(client_socket, chunk_str.c_str(), chunk_str.size(), 0);
    //         bytes_left -= bytes_to_send;
    //         bytes_sent += bytes_to_send;
    //     }

    //     // send the final chunk with size 0 to indicate the end of the response
    //     std::stringstream final_chunk;
    //     final_chunk << "0\r\n\r\n";
    //     std::string final_chunk_str = final_chunk.str();
    //     send(client_socket, final_chunk_str.c_str(), final_chunk_str.size(), 0);
    // }

    //consideration max_stale, max_age, min_fresh
    bool Is_fresh_req_resp(Request& req, Response& resp){
        if(resp.cache_control.Is_not_cacheable()||resp.cache_control.Is_need_revalidation()||\
        req.cache_control.Is_not_cacheable()||req.cache_control.Is_need_revalidation()) {
            return false;
        }
        time_t current_time = time(NULL);

        if(req.cache_control.max_age!=-1){
            //response's age smaller than time passed
            if(current_time-resp.resp_time<req.cache_control.max_age)
                return true;
            else
                return false;
        }
        if(req.cache_control.max_stale!=-1 && resp.cache_control.must_revalidate==false){
            //response haven't expire so much
            LOG_OUTPUT("req max_stale"<<std::endl;)
            if(current_time-resp.cache_control.expiration_time<req.cache_control.max_stale)
                return true;
            else
                return false;
        }
        if(req.cache_control.min_fresh!=-1){
            //response still fresh enough
            if(resp.cache_control.expiration_time-current_time>req.cache_control.min_fresh)
                return true;
            else
                return false;
        }
        if(current_time<=resp.cache_control.expiration_time)
            return true;
        return false;
    }

    //test max_age for request
    //test max_stale for request
    //test min_fresh for request
    bool Is_need_revalidation_req_resp(Request& req, Response& resp){
        if(req.cache_control.Is_need_revalidation()||resp.cache_control.Is_need_revalidation()){
            return true;
        }
        time_t current_time = time(NULL);
        //it really expired
        if(current_time>resp.cache_control.expiration_time){
            return false;
        }
        return true;
    }

}; //end of ClientHandler class




#endif