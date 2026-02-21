#ifndef HTTPREPLY_HPP
#define HTTPREPLY_HPP
#include "http_request.hpp"
#include "threadpool/event_handler.hpp"
#include <sys/uio.h>
class http_reply : public http_request{  
public:
    http_reply(){}
    ~http_reply(){}
    void init_reply(); // 初始化响应相关参数

    

private:
    bool add_response(const char* format, ...); // 往写缓冲区中添加响应报文数据
    bool add_status_line(int status, const char* title); // 添加状态行
    bool add_headers(int content_len); // 添加消息报头
    bool add_content(const char* content); // 添加响应正文
    bool add_content_type(); // 添加响应正文类型
    bool add_content_lengtn(int content_length); // 添加响应正文长度
    bool add_linger(); // 添加是否保持连接
    bool add_blank_line(); // 添加空行

 
    char write_buf_[WRITE_BUFFER_SIZE]; // 写缓冲区
    int write_idx_;                     // 写缓冲区中待发送的字节数
    
    
    http_message::REPLY_STATUS rep_status;  // 实例化响应状态
    struct stat rep_file_stat_;            // 目标文件的状态，从request中获取关于html文件的状态
    struct iovec iov[2];        // iovec结构体数组，其中第一个元素指向write_buf_，第二个元素指向rep_file_
    int iov_cnt_;           // iovec结构体数组的元素个数
    char* rep_html_file_addr_;         // 目标文件被mmap到内存中的起始位置
    int bytes_to_send_;         // 待发送的字节数
public:
    bool keep_alive_;                   // 是否保持连接
    int close_log_ = 0;                 // 关闭连接日志
    bool process_reply(HTTP_CODE ret);       // 处理生成响应内容
    bool write(int sockfd);  
    
    int get_iov_cnt() const { return iov_cnt_; }        // 获取 iovec 数组的元素个数
    struct iovec* get_iov() { return iov; }             // 获取 iovec 数组
    int get_bytes_to_send() const { return bytes_to_send_; }    // 获取待发送的字节数
    void set_file_info(const struct stat& st, char* addr){ rep_file_stat_ = st; rep_html_file_addr_ = addr; }   // 设置响应文件信息
};

// LF模式事件处理器
class LFHttpEventHandler : public EventHandler {
public:
    LFHttpEventHandler(http_reply* conn, SQLConnectionPool* sql_pool)
        : conn_(conn), sql_conn_pool_(sql_pool) {};
    ~LFHttpEventHandler();

    // 获取连接的文件描述符
    int get_handle() const override { return conn_->get_sockfd(); }

    // 处理事件
    void handle_event(uint32_t events) override {
        if (events & EPOLLIN) {
            // 处理读事件
            handle_read();
        }
        if (events & EPOLLOUT) {
            // 处理写事件
            handle_write();
        }
    }

private:
    void handle_read();   // 读取并处理HTTP请求
    void handle_write();  // 发送HTTP响应
    

    http_reply* conn_;
    SQLConnectionPool* sql_conn_pool_;
};
#endif // HTTPREPLY_HPP