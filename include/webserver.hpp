#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <strings.h>
#include <iostream>
#include <arpa/inet.h>
#include <vector>
#include <map>


#include "utils/utils.hpp"
#include "utils/timer.hpp"
#include "http/http_request.hpp"
#include "http/http_reply.hpp"
#include "CGImysql/sql_connection_pool.hpp"
#include "threadpool/thread_pool.hpp"
#include "threadpool/handle_set.hpp"
#include "log/log.hpp"

const int MAX_EVENT_NUMBER = 10000; // epoll事件表最大事件数
const int MAX_FD = 65536;           // 最大文件描述符数
const int TIMESLOT = 10;             // 最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

    // 初始化服务器
    void init(int port, int linger_mode, int trig_mode, int actor_mode, int concurrent_mode,
            int db_host, std::string db_user, std::string db_password, std::string db_name, int sql_num, int close_log, int log_write, int thread_num);
    // 设置监听
    void eventListen();
    // epoll触发模式设置
    void trigMode();
    // 日志设置
    void setLog();
    // 设置数据库连接池
    void setSqlConnPool();
    // 设置网络连接线程池
    void setConnThreadPool();
    // 处理读事件
    void handleReadEvent(int sock_fd);
    // 处理写事件
    void handleWriteEvent(int sock_fd);

    // 处理连接事件
    void handleConnEvent(int sock_fd);

    // 定时器设置
    void setClientTimer(int conn_fd, struct sockaddr_in client_addr);
    void cb_func(client_data *user_data);       // 超时回调函数

    // 新客户端连接处理
    bool acceptConnections();
    
    // 处理信号
    bool dealWithSignal(bool &time_out, bool &stop_server);

    // static bridge for C-style timer callback
    static void timer_bridge(client_data *user_data);

    // 服务器事件处理循环
    void eventLoop();
    // 事件处理测试
    void testEventLoop(int epoll_fd, int listen_fd);

    int conn_count_ = 0;                   // 统一维护活跃连接数

private:
    void adjust_timer(int sock_fd, util_timer *timer);   // 调整连接对应的定时器的连接时间
    void delete_timer(int sock_fd, util_timer *timer);   // 移除指定连接的定时器


public:
    int port_;              // 服务器端口号
    int listen_fd_;         // 监听套接字文件描述符
    int linger_mode_;       // close模式设置
    int trig_mode_;         // 触发模式设置
    int listen_trig_mode_ = 1;  // 监听事件触发模式（ET触发：1， LT触发：0）
    int conn_trig_mode_ = 1;    // 连接事件触发模式（ET触发：1， LT触发：0）
    int actor_mode_ = 0;        // 事件分发模型，0：reactor，1：proactor
    int concurrent_mode_ = 0;   // 并发模型，0：半同步/半异步模型，1：领导者/跟随者模型

    bool time_out_ = false;     // 超时标志
    bool stop_server_ = false;      // 服务关闭标志

    int close_log_ = 0;    // 关闭日志, 默认不关闭
    int log_write_ = 0;    // 日志写入方式, 默认同步
    bool log_asyc_ = true;     // 异步日志

    utilEpoll util_epoll_;
    int ep_fd_;                // epoll文件描述符 
    int pipe_fd_[2];           // 将信号安全的转发到主线程的epoll循环      
    struct epoll_event events[MAX_EVENT_NUMBER];   // 存放就绪的事件描述符


    http_reply* users_;          // http连接客户端对象数组（http_reply 继承自 http_request）
    char* html_root_;            // 网站根目录
    struct sockaddr_in client_addr_;

    // 数据库设置
    SQLConnectionPool *sql_conn_pool_;       // 数据库连接池
    int db_host_;
    std::string db_user_;
    std::string db_password_;
    std::string db_name_;
    int sql_num_;

    // 网络连接线程池
    ThreadPool<http_reply> *conn_thread_pool_;
    int thread_num_;
    
    HandleSet* handle_set_;    // 事件处理器集合

    // 定时器
    client_data *users_data_;               // 连接资源（客户端文件描述符+客户端地址+客户端对应的定时器）
    conn_timer_manager *timer_manager_;    // 所有连接对应的定时器管理
    // static instance for callback bridge
    static WebServer* instance;
};
#endif // WEBSERVER_HPP