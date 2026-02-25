#ifndef LOG_HPP
#define LOG_HPP

#include <stdio.h>
#include <iostream>
#include <string>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>

#include "lock/locker.hpp"
#include "utils/block_queue.hpp"
class Log{
public:
    // 单例模式
    static Log *get_instance(){
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args){
    Log::get_instance()->async_write_log();
    return nullptr;
    }
    // 初始化
    bool init(const char* file_name, int close_log, int log_buf_size = 8192, int max_lines = 5000000, int max_queue_size = 0);

    bool write_log(std::string level, const char *format, ...);

    void flush(void);

    // 是否启用日志输出（close_log_==0 表示启用）
    bool is_enabled() const { return close_log_ == 0; }

private:
    Log();
    virtual ~Log();

    void *async_write_log()
    {
        std::string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (log_queue_->pop(single_log))
        {
            mutex_.lock();
            fputs(single_log.c_str(), fp_);
            mutex_.unlock();
        }

    return nullptr;
    }

private:
    char dir_name_[128];      // 日志存放路径
    char log_name_[128];      // 日志文件名称
    int log_max_lines_;         // 日志文件最大行数
    int log_buffer_size_;        // 日志缓冲区大小
    long long log_lines_counts_;    // 日志行数
    int today_;                 // 记录当前时间所处日期，日志按天归类
    FILE *fp_;                  // 打开log的文件指针
    char *buffer_;              // 缓冲区
    MutexLock mutex_;           // 互斥锁
    int close_log_;             // 关闭日志, true:关闭日志，false:不关闭日志
    bool is_async_;              // 是否同步标志位

    block_queue<std::string> *log_queue_;   // 日志信息的阻塞队列
};

#define LOG_DEBUG(format, ...) if(Log::get_instance()->is_enabled()) { Log::get_instance()->write_log("Debug", format, ##__VA_ARGS__); Log::get_instance()->flush(); } 
#define LOG_INFO(format, ...)  if(Log::get_instance()->is_enabled()) { Log::get_instance()->write_log("Info",  format, ##__VA_ARGS__); Log::get_instance()->flush(); } 
#define LOG_WARN(format, ...)  if(Log::get_instance()->is_enabled()) { Log::get_instance()->write_log("Warn",  format, ##__VA_ARGS__); Log::get_instance()->flush(); } 
#define LOG_ERROR(format, ...) if(Log::get_instance()->is_enabled()) { Log::get_instance()->write_log("Error", format, ##__VA_ARGS__); Log::get_instance()->flush(); } 

#endif 