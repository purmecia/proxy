#pragma once

#ifndef HTTP_RESPONSE_PASER_HPP
#define HTTP_RESPONSE_PASER_HPP

#include <sstream>
#include <vector>
#include <iostream>
#include <ctime>
#include "common.hpp"

// Deal with response related cache-control header and Etag header and Last-Modified header
class Response_cache_control {
public:
    bool no_cache;
    bool no_store;
    bool must_revalidate;
    bool proxy_revalidate;
    bool public_cache;
    bool private_cache;
    bool no_transform;
    int max_age;
    int s_max_age;
    std::string Etag;
    std::string last_modified;
    std::string reason;
    time_t expiration_time;

    Response_cache_control() {
        no_cache = false;
        no_store = false;
        must_revalidate = false;
        proxy_revalidate = false;
        public_cache = false;
        private_cache = false;
        no_transform = false;
        max_age = 600; //heuristic value :)
        s_max_age = -1;
        Etag = "";
        last_modified = "";
        reason = "";
        expiration_time = -1;
    }

    void Parse_cache_control_header(std::map<std::string, std::string>& headers_map, time_t& received_time) {
        //set default expiration time
        expiration_time = received_time + max_age;
        //parse cache-control header
        if(headers_map.find("Cache-Control") != headers_map.end()) {
            std::string& cache_control_header=headers_map["Cache-Control"];
            std::istringstream cache_control_stream(cache_control_header);
            std::string cache_control_line;
            
            while (std::getline(cache_control_stream, cache_control_line, ',')) {
                // std::cout<<"[DEBUG] cache_control_line: "<<cache_control_line<<std::endl;
                if (cache_control_line.find("no-cache") != std::string::npos) {
                    // std::cout<<"[DEBUG] set no-cache is true"<<std::endl;
                    no_cache = true;
                }
                if(cache_control_line.find("no-store") != std::string::npos) {
                    no_store = true;
                } 
                if (cache_control_line.find("must-revalidate") != std::string::npos) {
                    must_revalidate = true;
                } 
                if (cache_control_line .find("proxy-revalidate") != std::string::npos) {
                    proxy_revalidate = true;
                } 
                if (cache_control_line.find("public") != std::string::npos) {
                    public_cache = true;
                } 
                if (cache_control_line.find("private") != std::string::npos) {
                    private_cache = true;
                } 
                if (cache_control_line.find("no-transform") != std::string::npos) {
                    no_transform = true;
                } 
                if (cache_control_line.find("max-age") != std::string::npos) {
                    max_age = std::stoi(cache_control_line.substr(cache_control_line.find("=")+1));
                    expiration_time = received_time + max_age;
                } 
                if (cache_control_line.find("s-maxage") != std::string::npos) {
                    s_max_age = std::stoi(cache_control_line.substr(cache_control_line.find("=")+1));
                    expiration_time = received_time + s_max_age;
                }
            }
        }
        //parse Etag header
        if(headers_map.find("Etag") != headers_map.end()) {
            Etag = headers_map["Etag"];
        }
        //parse Last-Modified header
        if(headers_map.find("Last-Modified") != headers_map.end()) {
            last_modified = headers_map["Last-Modified"];
        }
    }

    // Return true if the response is cacheable
    bool Is_not_cacheable() {
        if(no_store) {
            reason = "response is no-store";
            return true;
        }
        if(private_cache) {
            reason = "response is private";
            return true;
        }
        return false;
    }

    // Return true if the response need revalidation
    bool Is_need_revalidation() {
        if(no_cache || proxy_revalidate||max_age==0) {
            return true;
        }
        return false;
    }
    

    // Return true if the response is fresh
    // bool Is_fresh() {
    //     if(Is_not_cacheable()||Is_need_revalidation()) {
    //         return false;
    //     }
    //     time_t current_time = time(NULL);
    //     if(current_time < expiration_time) {
    //         return true;
    //     }
    //     return false;
    // }
};

enum Reponse_error_code{
    OK = 0,
    INVALID_RESPONSE = 1,
    INVALID_RESPONSE_FIRST_LINE = 2,
    INVALID_RESPONSE_HEADER = 3,
    RESPONSE_NOT_IN_CACHE = 4,
    UNEXPECTED_ERROR = 5,
    INVALID_CONTENT_LENGTH = 6,
    INCOMPLETE_RESPONSE = 7,
};

struct Response {
    std::string version;
    int status_code;
    std::string status_name;
    // std::vector<std::pair<std::string, std::string>> headers;
    std::map<std::string, std::string> headers_map;
    std::string body;

    std::string raw_data;
    int content_length;
    Response_cache_control cache_control;

    time_t resp_time;
    Reponse_error_code error_code;

    bool is_chunked;
};

Response Parsing_HTTP_response(char* raw_response, int bytes,time_t received_time,int& Connect_id) {
    Response resp;

    resp.resp_time = received_time;
    // std::cout<<std::endl;
    // std::cout<<"Parsing HTTP response"<<std::endl;
    // std::cout<<"Bytes received: "<<bytes<<std::endl;
    std::string raw_str(raw_response, bytes);

    resp.raw_data = raw_str;
    // std::cout<<"Raw data: "<<raw_str<<std::endl;

    size_t first_line_end = raw_str.find("\r\n");
    if (first_line_end == std::string::npos) {
        resp.error_code = INVALID_RESPONSE_FIRST_LINE;
        LOG_OUTPUT(PRINTIDERR(Connect_id) "Invalid HTTP response: missing first line" << std::endl;)
        LOG_OUTPUT(PRINTIDERR(Connect_id) "When missing, response: "<<raw_str<<std::endl;)
        //To-do: throw exception
        return resp;
    }   

    // Parse the first line
    std::istringstream first_line_stream(raw_str.substr(0, first_line_end));
    std::string status_code_tmp;
    first_line_stream >> resp.version >> status_code_tmp;
    resp.status_code = std::stoi(status_code_tmp);
    std::getline(first_line_stream.ignore(1), resp.status_name); 

    // Parse the headers
    size_t header_start = first_line_end + 2;
    size_t header_end = raw_str.find("\r\n\r\n", header_start);
    if (header_end == std::string::npos) {
        // possible 304 response
        if(resp.status_code != 304) {
            LOG_OUTPUT(PRINTIDERR(Connect_id) "Invalid HTTP response: missing headers" << std::endl;)
            LOG_OUTPUT(PRINTIDERR(Connect_id) "When missing, response: "<<raw_str<<std::endl;)
            resp.error_code = INVALID_RESPONSE_HEADER;
            return resp;
        }
    }

    std::string headers_str = raw_str.substr(header_start, header_end - header_start);
    std::istringstream headers_stream(headers_str);
    std::string header_line;
    while (std::getline(headers_stream, header_line)) {
        // LOG_OUTPUT(header_line << std::endl;)
        size_t separator_pos = header_line.find(": ");
        if (separator_pos == std::string::npos||separator_pos+2>=header_line.length()) {
            resp.error_code = INVALID_RESPONSE_HEADER;
            LOG_OUTPUT(PRINTIDERR(Connect_id) "Invalid header: " << header_line << std::endl;)
            LOG_OUTPUT(PRINTIDERR(Connect_id) "When missing, response: "<<raw_str<<std::endl;)
            return resp;
        }
        std::string header_name = header_line.substr(0, separator_pos);
        std::string header_value = header_line.substr(separator_pos + 2); 
        // resp.headers.push_back(std::make_pair(header_name, header_value));
        resp.headers_map[header_name] = header_value;
    }

    // Parse the body
    size_t body_start = header_end + 4;
    resp.body = raw_str.substr(body_start);

    if(resp.headers_map.find("Content-Length") != resp.headers_map.end())
        resp.content_length = std::stoi(resp.headers_map["Content-Length"]);
    // else{
    //     resp.is_chunked = true;
    // }
    //inidicate whether chunked
    if(resp.headers_map.find("Transfer-Encoding") != resp.headers_map.end()) {
        if(resp.headers_map["Transfer-Encoding"].find("chunked") != std::string::npos) {
            resp.is_chunked = true;
        }
    }

    //parse the cache control
    resp.cache_control.Parse_cache_control_header(resp.headers_map, resp.resp_time);
    resp.error_code = OK;
    return resp;
}

void Print_response(Response& resp) {
    LOG_OUTPUT( "version: " << resp.version << std::endl;)
    LOG_OUTPUT("status_code: " << resp.status_code << std::endl;)
    LOG_OUTPUT( "status_name: " << resp.status_name << std::endl;)

    for(auto& header : resp.headers_map) {
        LOG_OUTPUT( header.first << ": " << header.second << std::endl;)
    }
    LOG_OUTPUT("body: " << resp.body << std::endl;)
    LOG_OUTPUT(std::endl;)
}

// format the response to be sent to client
std::string Format_response(Response& resp) {
    std::string response = resp.version + " " + std::to_string(resp.status_code) + " " + resp.status_name + "\r\n";
    for (auto& header : resp.headers_map)
    {
        response += header.first + ": " + header.second + "\r\n";
    }
    response += "\r\n";
    response += resp.body;
    return response;
}

// update saved response with new response regarding headers
void Update_response(Response& saved_resp, Response& new_resp) {
    //update time
    saved_resp.resp_time = new_resp.resp_time;
    //update expiration time
    saved_resp.cache_control.expiration_time = new_resp.cache_control.expiration_time;
    //update etag
    if(new_resp.cache_control.Etag != "") {
        saved_resp.cache_control.Etag = new_resp.cache_control.Etag;
    }
    //update last modified
    if(new_resp.cache_control.last_modified != "") {
        saved_resp.cache_control.last_modified = new_resp.cache_control.last_modified;
    }

}

// check whether chunked data received is complete
// bool Is_chunked_data_complete(std::string& chunked_data) {
//     size_t pos = chunked_data.find("\r\n");
//     if(pos == std::string::npos) {
//         return false;
//     }
//     std::string chunk_size_str = chunked_data.substr(0, pos);
//     int chunk_size = std::stoi(chunk_size_str, nullptr, 16);
//     if(chunk_size == 0) {
//         return true;
//     }
//     if(chunked_data.size() < pos + 2 + chunk_size + 2) {
//         return false;
//     }
//     return true;
// }

// // check the size of response body same as content-length
// bool Is_content_length_wrong(Response& resp) {
//     if(resp.status_code==200 && !resp.is_chunked && resp.content_length !=0){
//         if(resp.content_length >= 500 + resp.body.size()) {
//             return true;
//         }
//     }
//     return false;
// }

#endif