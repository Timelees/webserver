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
#include "http/http_request.hpp"
#include "http/http_reply.hpp"
#include "CGImysql/sql_connection_pool.hpp"
#include "threadpool/thread_pool.hpp"

const int MAX_EVENT_NUMBER = 10000; // epoll事件表最大事件数
const int MAX_FD = 65536;           // 最大文件描述符数

class WebServer {
public:
    WebServer();
    ~WebServer();

    // 初始化服务器
    void init(int port, int linger_mode, int trig_mode, int actor_mode, int concurrent_mode,
            int db_host, std::string db_user, std::string db_password, std::string db_name, int sql_num);
    // 设置监听
    void eventListen();
    // epoll触发模式设置
    void trigMode(int trig_mode_);
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

    // 事件处理测试
    void testEventLoop(int epoll_fd, int listen_fd);

public:
    int port_;              // 服务器端口号
    int listen_fd_;         // 监听套接字文件描述符
    int linger_mode_;       // close模式设置
    int trig_mode_;         // 触发模式设置(1：ET, 0:LT)
    int listen_trig_mode_;  // epoll监听触发模式
    int actor_mode_ = 0;        // 事件分发模型，0：reactor，1：proactor
    int concurrent_mode_ = 0;   // 并发模型，0：半同步/半异步模型，1：领导者/跟随者模型

    bool close_log_ = true;    // 关闭日志

    utilEpoll util_epoll_;
    int ep_fd_;                // epoll文件描述符      
    struct epoll_event events[MAX_EVENT_NUMBER];   // 存放就绪的事件描述符


    http_reply* users_;          // http连接对象数组（http_reply 继承自 http_request）
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
};
#endif // WEBSERVER_HPP