#pragma once

#ifndef HTTP_IN_REQUEST_PASER_HPP
#define HTTP_IN_REQUEST_PASER_HPP


#include<string>
#include<vector>
#include<sstream>
#include<iostream>
#include<map>
#include<ctime>
#include<algorithm>

#include"common.hpp"
//Deal with request cache contorl and if modified since and if none match
class Request_cache_control {
public:
    bool only_if_cached;
    bool no_cache;
    bool no_store;
    bool no_transform;
    int max_age;
    int min_fresh;
    int max_stale;
    std::string if_modified_since;
    std::string if_none_match;
    std::string reason;

    Request_cache_control(){
        only_if_cached = false;
        no_cache = false;
        no_store = false;
        no_transform = false;
        max_age = -1;
        min_fresh = -1;
        max_stale = -1;
        if_modified_since = "";
        if_none_match = "";
        reason="";
    }

    void Parse_cache_control(std::map<std::string, std::string>& headers_map){
        //parse cache control
        if (headers_map.find("Cache-Control") != headers_map.end()) {
            std::string& cache_control_header=headers_map["Cache-Control"];
            std::istringstream cache_control_stream(cache_control_header);
            std::string cache_control_line;
            while(std::getline(cache_control_stream, cache_control_line, ',')){
                if(cache_control_line.find("only-if-cached")!=std::string::npos){
                    only_if_cached = true;
                }
                else if(cache_control_line.find("no-cache")!=std::string::npos){
                    no_cache = true;
                }
                else if(cache_control_line.find("no-store")!=std::string::npos){
                    no_store = true;
                    reason="request is no-store";
                }
                else if(cache_control_line.find("no-transform")!=std::string::npos){
                    no_transform = true;
                }
                else if(cache_control_line.find("max-age")!=std::string::npos){
                    max_age = std::stoi(cache_control_line.substr(cache_control_line.find("=")+1));
                }
                else if(cache_control_line.find("min-fresh")!=std::string::npos){
                    min_fresh = std::stoi(cache_control_line.substr(cache_control_line.find("=")+1));
                }
                else if(cache_control_line.find("max-stale")!=std::string::npos){
                    max_stale = std::stoi(cache_control_line.substr(cache_control_line.find("=")+1));
                }
            }
        }
        //parse if modified since
        if (headers_map.find("If-Modified-Since") != headers_map.end()) {
            if_modified_since = headers_map["If-Modified-Since"];
        }
        //parse if none match
        if (headers_map.find("If-None-Match") != headers_map.end()) {
            if_none_match = headers_map["If-None-Match"];
        }
    }

    //return true if cacheable
    bool Is_not_cacheable(){
        if( no_store){
            return true;
        }
        return false;
    }

    bool Is_need_revalidation() {
        if(no_cache) {
            return true;
        }
        return false;
    }
};

enum Request_error_code{
    NO_ERROR,
    BAD_REQUEST,
    MISSING_FIRST_LINE,
    MISSING_HEADERS,
    INVAID_HEADERS
};


//write a parser to parse the incoming request
struct Request {
    std::string method;
    std::string url;
    std::string version;
    std::string host;
    int port;
    time_t req_time;
    struct sockaddr_in client_addr;
    std::vector<std::pair<std::string, std::string>> headers;
    std::map<std::string, std::string> headers_map;
    std::string body;
    Request_cache_control cache_control;

    std::string raw_request;
    int content_length;
    Request_error_code error_code;
};

Request Parsing_HTTP_request(char* raw_request, int bytes,time_t received_time, int& Connect_id) {
    Request req;

    //set the request time
    req.req_time = received_time;
    // Convert char* to string
    std::string raw_str(raw_request, bytes);
    req.raw_request = raw_str;
    // std::cout<<"Received request:"<<raw_str<<std::endl;
    
    //parse the first line

    // Find the end of the first line
    size_t first_line_end = raw_str.find("\r\n");
    if (first_line_end == std::string::npos) {
        LOG_OUTPUT(PRINTIDERR(Connect_id) "Invalid HTTP request: missing first line" << std::endl;)
        LOG_OUTPUT(PRINTIDERR(Connect_id)"When missing, byte: "<<bytes<<std::endl;)
        LOG_OUTPUT(PRINTIDERR(Connect_id) "When missing, req: "<<raw_str<<std::endl;)
        req.error_code=MISSING_FIRST_LINE;
        return req;
    }    
    // Parse the first line
    std::stringstream first_line_stream(raw_str.substr(0, first_line_end));
    first_line_stream >> req.method >> req.url >> req.version;

    // std::cout << "method: " << req.method << std::endl;
    // std::cout << "url: " << req.url << std::endl;
    // std::cout << "version: " << req.version << std::endl;

    // Parse the URL to extract the host and port
    size_t host_start = 0;
    if(req.url.find("://")!=std::string::npos){
        host_start = req.url.find("://")+3;
    }

    size_t host_end = req.url.find(":", host_start);

    int  colonmn_count = std::count(req.url.begin()+host_start, req.url.end(), ':');

    //if only have one ':'
    if(colonmn_count==1){
        if (host_end == std::string::npos){//||req.method=="GET"||req.method=="POST") {
            host_end = req.url.find("/", host_start);
            if (host_end == std::string::npos) {
                host_end = req.url.length();
            }
        }
    }
    else{
        if (host_end == std::string::npos||req.method=="GET"||req.method=="POST") {
            host_end = req.url.find("/", host_start);
            if (host_end == std::string::npos) {
                host_end = req.url.length();
            }
        }        
    }


    req.host = req.url.substr(host_start, host_end - host_start);
    // std::cout << "host: " << req.host << std::endl;
    size_t port_start = host_end;
    if (port_start < req.url.length() && req.url[port_start] == ':') {
        ++port_start;
        std::istringstream port_stream(req.url.substr(port_start));
        port_stream >> req.port;
    } else {
        req.port = 80; // default HTTP port
    }
    

    // Parse the headers
    size_t header_start = first_line_end + 2;
    size_t header_end = raw_str.find("\r\n\r\n", header_start);
    if (header_end == std::string::npos) {
        LOG_OUTPUT("Invalid HTTP request: missing headers" << std::endl;)
        req.error_code=MISSING_HEADERS;
        return req;
    }
    std::string headers_str = raw_str.substr(header_start, header_end - header_start);
    std::istringstream headers_stream(headers_str);
    std::string header_line;
    while (std::getline(headers_stream, header_line)) {
        size_t separator_pos = header_line.find(": ");
        if (separator_pos == std::string::npos) {
            LOG_OUTPUT(PRINTIDERR(Connect_id)  "Invalid header: " << header_line << std::endl;)
            req.error_code=INVAID_HEADERS;
            return req;
        }
        std::string header_name = header_line.substr(0, separator_pos);
        std::string header_value = header_line.substr(separator_pos + 2); 
        req.headers.push_back(std::make_pair(header_name, header_value));
        req.headers_map[header_name] = header_value;
    }


    //parse the cache control
    req.cache_control.Parse_cache_control(req.headers_map);


    // Parse the body
    size_t body_start = header_end + 4;
    req.body = raw_str.substr(body_start);

    //parse the content length
    if(req.headers_map.find("Content-Length")!=req.headers_map.end()){
        req.content_length = std::stoi(req.headers_map["Content-Length"]);
        // If body size smaller than content length, return error
        if(req.method=="POST" && req.body.size()>req.content_length){
            req.error_code = BAD_REQUEST;
            return req;
        }
    }



    return req;
}

void Print_request(Request& req) {
    LOG_OUTPUT( "method: " << req.method << std::endl;)
    LOG_OUTPUT( "url: " << req.url << std::endl;)
    LOG_OUTPUT( "version: " << req.version << std::endl;)
    LOG_OUTPUT("host: " << req.host << std::endl;)
    LOG_OUTPUT("port: " << req.port << std::endl;)
    for (auto& header : req.headers)
    {
        LOG_OUTPUT(header.first << ": " << header.second << std::endl;)
    }
    LOG_OUTPUT("body: " << req.body << std::endl;)
}



#endif
