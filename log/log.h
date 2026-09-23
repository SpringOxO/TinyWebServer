#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <string>
#include <cstdarg>
#include <thread>
#include <memory>
#include <mutex>
#include <fstream>
#include "block_queue.h"

//using namespace std;

class Log
{
public:
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    //可选择的参数有日志文件、最大行数以及最长日志条队列
    bool init(const std::string& file_name, int close_log, int split_lines = 5000000, int max_queue_size = 0);
    void write_log(int level, const char *format, ...);
    void flush(void);

    bool IsOpen() const { 
        return m_close_log == 0; 
    }

private:
    Log() : m_count(0), m_is_async(false), m_close_log(0) {}
    virtual ~Log();
    void async_write_log();

private:
    std::string m_dir_name; //路径名
    std::string m_log_name; //log文件名
    int m_split_lines;  //日志最大行数
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天

    std::ofstream m_file;         //优化：文件流
    
    std::unique_ptr<block_queue<std::string>> m_log_queue;
    std::unique_ptr<std::thread> m_write_thread;

    bool m_is_async;                  //是否同步标志位
    std::mutex m_mutex;
    int m_close_log; //关闭日志
};

#define LOG_DEBUG(format, ...) do { if(Log::get_instance()->IsOpen()) { Log::get_instance()->write_log(0, format, ##__VA_ARGS__); } } while(0)
#define LOG_INFO(format, ...)  do { if(Log::get_instance()->IsOpen()) { Log::get_instance()->write_log(1, format, ##__VA_ARGS__); } } while(0)
#define LOG_WARN(format, ...)  do { if(Log::get_instance()->IsOpen()) { Log::get_instance()->write_log(2, format, ##__VA_ARGS__); } } while(0)
#define LOG_ERROR(format, ...) do { if(Log::get_instance()->IsOpen()) { Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush(); } } while(0) // 只有 Error 级别强制刷盘


#endif
