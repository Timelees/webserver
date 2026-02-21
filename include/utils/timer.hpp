#ifndef TIMER_HPP
#define TIMER_HPP

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <list>

#include <time.h>
#include <iostream>

#include "log/log.hpp"

// LOG_INFO/LOG_ERROR 宏依赖一个名为 close_log_ 的变量。
// 其他模块（如 WebServer）通常有成员 close_log_，但定时器模块是独立编译单元，
// 在此提供一个“兜底”开关，避免在 timer.cpp 使用 LOG_INFO 时出现未定义符号。
// 语义：close_log_==0 表示开启日志；==1 表示关闭日志。
class util_timer;

// 连接资源
struct client_data
{
    sockaddr_in address; // 客户端地址
    int sockfd;          // 客户端socket文件描述符
    util_timer *timers;  // 定时器
};

// 定时器
class util_timer
{
public:
    util_timer() {}

public:
    time_t expire; // 超时时间

    client_data *user_data_{nullptr};
    void (*cb_func_)(client_data *){nullptr}; // 回调函数，指向定时事件
};

// 连接定时器管理，维护所有连接的定时器
class conn_timer_manager
{
public:
    conn_timer_manager(int timeSlot) : time_slot(timeSlot) {}
    ~conn_timer_manager() {}

public:
    void add_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void resort_timer(util_timer *timer);
    void timer_handler();
    void tick();

    void print_timer(); // 测试函数，显示当前的定时器信息

private:
    std::list<util_timer *> timers_list_;
    int time_slot;
};

#endif