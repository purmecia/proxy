#pragma once
#ifndef CACHE_HPP
#define CACHE_HPP
#include<map>
#include<mutex>
#include<ctime>
#include<thread>
#include<chrono>
#include <functional>

#include"http_response_parser.hpp"
#include"common.hpp"

#define CLEAN_INTERVAL 60

//create a class for cache HTTP response

//insert a response into cache

int cache_hash(std::string key){
    return std::hash<std::string>{}(key)%30;
}

class proxy_cache{

public:
    std::mutex cache_mutex[30]; 
    std::map<std::string, Response> cache_map;

    proxy_cache();
    ~proxy_cache();
    void insert_cache(int Connect_id, std::string key, Response resp);
    void delete_cache(int Connect_id, std::string key);
    Response find_cache(int Connect_id, std::string key);
    // void delete_expired_cache();
    bool IS_not_in_cache(int Connect_id, std::string key);

};

proxy_cache::proxy_cache(){
    // std::thread cache_cleaner(delete_expired_cache);
    // threads.emplace_back(&cache_cleaner);
}

proxy_cache::~proxy_cache(){
    cache_map.clear();
}

//whether in cache
bool proxy_cache::IS_not_in_cache(int Connect_id, std::string req_url){
    try{
        // std::lock_guard<std::mutex> lock(cache_mutex[req_url[0]-'a']);
        if(cache_map.find(req_url) != cache_map.end()){
            return false;
        }
        return true;
    }catch(...){
        LOG_OUTPUT(PRINTIDERR(Connect_id) "Error in finding cache"<<std::endl;)
        return false;
    }
}

//inssert a response into cache
void proxy_cache::insert_cache(int Connect_id, std::string req_url, Response resp){
    try{
        // std::lock_guard<std::mutex> lock(cache_mutex[req_url[0]-'a']);
        cache_map[req_url] = resp;
        LOG_OUTPUT(PRINTID(Connect_id) "Update cache regarding "<<req_url<<std::endl;)
        
    }catch(...){
        LOG_OUTPUT(PRINTIDERR(Connect_id) "Error in inserting cache"<<std::endl;)
    }
}

//delete a response from cache
void proxy_cache::delete_cache(int Connect_id, std::string req_url){
    try{
        // std::lock_guard<std::mutex> lock(cache_mutex[req_url[0]-'a']);
        cache_map.erase(req_url);
    }catch(...){
        LOG_OUTPUT(PRINTIDERR(Connect_id) "Error in deleting cache"<<std::endl;)
    }

}

//find a response from cache
Response proxy_cache::find_cache(int Connect_id, std::string req_url){
    try{
        // std::lock_guard<std::mutex> lock(cache_mutex[req_url[0]-'a']);
        if(cache_map.find(req_url) != cache_map.end()){
            return cache_map[req_url];
        }
        else{
            Response resp;
            return resp;
        }
    }catch(...){
        LOG_OUTPUT(PRINTIDERR(Connect_id) "Error in finding cache"<<std::endl;)
        Response resp;
        return resp;
    }
}

static proxy_cache cache;

// delete expired cache
static void delete_expired_cache(){
    while(true){
        std::this_thread::sleep_for(std::chrono::seconds(CLEAN_INTERVAL));
        for(auto it = cache.cache_map.begin(); it != cache.cache_map.end();){
            try{
                std::lock_guard<std::mutex> lock(cache.cache_mutex[cache_hash(it->first)]);
                if(it->second.cache_control.expiration_time < time(NULL)){
                    LOG_OUTPUT("[No-id]: Delete expired cache regarding "<<it->first<<std::endl;)
                    it = cache.cache_map.erase(it);
                }
                else{
                    it++;
                }
            }catch(...){
                LOG_OUTPUT("Error in deleting expired cache"<<std::endl;)
            }
        }
    }
}


#endif