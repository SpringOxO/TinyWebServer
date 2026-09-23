#include "log.h"
#include "block_queue.h"
#include <chrono>    // 替代 sys/time.h
#include <cstdio>
#include <ctime>     // 替代 time.h
#include <iomanip>   // 引入 C++ 格式化控制符
#include <ios>
#include <memory>
#include <mutex>
#include <sstream>   // 引入字符串流拼接
#include <string>
#include <thread>

Log::~Log(){
    if (m_is_async && m_log_queue){
        m_log_queue -> close();
        if (m_write_thread && m_write_thread->joinable()) {
            m_write_thread->join();
        }
    }
    if (m_file.is_open()){
        m_file.close();
    }
}

bool Log::init(const std::string& file_name, int close_log, int split_lines, int max_queue_size) {
    m_close_log = close_log;
    
    // 如果用户要求关闭日志，直接退出
    if (m_close_log == 1) {
        return true; 
    }

    if (max_queue_size > 0){
        m_is_async = true;
        m_log_queue = std::make_unique<block_queue<std::string>>(max_queue_size);
        m_write_thread = std::make_unique<std::thread>(&Log::async_write_log, this);
    }
    m_close_log = close_log;
    m_split_lines = split_lines;
    
    auto now = std::chrono::system_clock().now();
    std::time_t t = std::chrono::system_clock().to_time_t(now);

    struct tm my_tm; //获取当前的本地时间
    localtime_r(&t, &my_tm);

    size_t p = file_name.find_last_of('/');
    std::ostringstream oss; // 【优化】使用 C++ 流拼装字符串，杜绝越界

    if (p == std::string::npos){
        m_log_name = file_name;
        oss << my_tm.tm_year + 1900 << "_" 
            << std::setfill('0') << std::setw(2) << my_tm.tm_mon + 1 << "_" 
            << std::setfill('0') << std::setw(2) << my_tm.tm_mday << "_" 
            << file_name;
    }
    else {
        m_log_name = file_name.substr(p + 1);
        m_dir_name = file_name.substr(0, p + 1);
        oss << m_dir_name << my_tm.tm_year + 1900 << "_" 
            << std::setfill('0') << std::setw(2) << my_tm.tm_mon + 1 << "_" 
            << std::setfill('0') << std::setw(2) << my_tm.tm_mday << "_" 
            << file_name;
    }
    
    m_today = my_tm.tm_mday;
    m_file.open(oss.str(), std::ios_base::app);
    //printf("filename = %s\n", oss.str().data());
    LOG_INFO("Starting log: %s", oss.str().data());
    return m_file.is_open();
}

void Log::write_log(int level, const char *format, ...){
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto usec = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;

    struct tm my_tm;
    localtime_r(&t, &my_tm);

    std::string s;
    switch (level) {
        case 0: s = "[debug]:"; break;
        case 1: s = "[info]:"; break;
        case 2: s = "[warn]:"; break;
        case 3: s = "[erro]:"; break;
    }

    { // 细粒度锁，只针对文件更换的部分
        std::lock_guard<std::mutex> lock(m_mutex);
        m_count++;
        if (my_tm.tm_mday != m_today || m_count >= m_split_lines){ // 如果日期变化或者行数满了
            m_file.flush();
            m_file.close();

            std::ostringstream tail_oss;
            tail_oss << my_tm.tm_year + 1900 << "_" 
                     << std::setfill('0') << std::setw(2) << my_tm.tm_mon + 1 << "_" 
                     << std::setfill('0') << std::setw(2) << my_tm.tm_mday << "_";
            std::string new_log = m_dir_name + tail_oss.str() + m_log_name;
            
            if (my_tm.tm_mday != m_today){ //更新日期重置今日行数
                m_today = my_tm.tm_mday;
                m_count = 0;
            }
            else { //添加后缀
                new_log += "_" + std::to_string(m_count / m_split_lines);
            }
            m_file.open(new_log, std::ios_base::app);
        }
    }

    std::ostringstream head_oss;
    head_oss << my_tm.tm_year + 1900 << "-" 
             << std::setfill('0') << std::setw(2) << my_tm.tm_mon + 1 << "-" 
             << std::setfill('0') << std::setw(2) << my_tm.tm_mday << " " 
             << std::setfill('0') << std::setw(2) << my_tm.tm_hour << ":" 
             << std::setfill('0') << std::setw(2) << my_tm.tm_min << ":" 
             << std::setfill('0') << std::setw(2) << my_tm.tm_sec << "." 
             << std::setfill('0') << std::setw(6) << usec << " " 
             << s << " ";

    va_list valst;
    va_start(valst, format);

    va_list valst_copy;
    va_copy(valst_copy, valst);
    // 传入 nullptr 探测实际所需长度
    int required_len = std::vsnprintf(nullptr, 0, format, valst_copy);
    va_end(valst_copy);

    std::string log_body;
    if (required_len > 0) {
        log_body.resize(required_len + 1); // +1 容纳末尾的 \0
        std::vsnprintf(&log_body[0], required_len + 1, format, valst);
        log_body.pop_back();
    }
    va_end(valst);
    std::string full_log = head_oss.str() + log_body + "\n";

    //printf("fulllog: %s\n", full_log.data());

    if (m_is_async){
        m_log_queue->push(full_log);
    }
    else {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_file << full_log;
    }
}

void Log::flush (){
    std::lock_guard<std::mutex> lock(m_mutex);
    m_file.flush();
}

void Log::async_write_log(){
    std::string str;
    while (m_log_queue->pop(str)){
        std::lock_guard<std::mutex> lock(m_mutex);
        m_file << str;
        m_file.flush(); // TODO 权宜之计，立即刷盘，性能较低
    }
}