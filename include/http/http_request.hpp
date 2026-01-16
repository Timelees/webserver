#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <iostream>
#include <string.h>
#include <string>
#include <map>
#include <sys/types.h> 
#include <sys/stat.h> 
#include <fcntl.h>
#include <sys/mman.h>

#include "http_message.hpp"
#include "CGImysql/sql_connection_pool.hpp"
#include "lock/locker.hpp"
#include "utils/utils.hpp"

class http_request : public http_message{  
public:
    http_request(){}
    ~http_request(){}

    http_message::HTTP_CODE test_function(int sockfd, const sockaddr_in& addr, char* root);
    void init(int sockfd, const sockaddr_in& addr, char* root); // 初始化客户端连接
    bool read_buffer();   // 读取客户端数据，将报文数据存入读缓冲区

    void init_mysql(SQLConnectionPool *conn_pool);

    void close_connection(bool real_close);

    http_message::HTTP_CODE process_request();                            // 解析HTTP请求（供线程池调用）

private:
    void init();        // 初始化参数

    http_message::LINE_STATUS parse_line();                             // 解析一行，判断依据\r\n
    http_message::HTTP_CODE parse_request_line(char* text);             // 解析请求状态行
    http_message::HTTP_CODE parse_request_headers(char* text);          // 解析请求头部
    http_message::HTTP_CODE parse_request_content(char* text);          // 解析请求实体主体

    http_message::HTTP_CODE do_request();   

    
    void unmap();


private:
    int sockfd_;                    // 该http请求对应的客户端连接描述符
    struct sockaddr_in addr_;

    http_message::METHOD method_;   // 请求方法
    char* host_;                    // 主机名
    char* url_;                     // 请求资源路径
    char* version_;                 // HTTP版本号
    char* content_;                 // 请求体
    int content_length_;            // 请求体长度
    bool keep_alive_;               // 是否保持连接

    http_message::CHECK_STATE check_state_;   // 主状态机当前所处的状态

    char read_buf_[READ_BUFFER_SIZE];   // 读缓冲区
    int read_idx_;                      // 读缓冲区中数据的最后一个字节


    int checked_idx_;                   // 当前正在分析的字符在读缓冲区中的位置
    int start_line_;                    // 当前正在解析的行的起始位置
    std::map<std::string, std::string> headers_;       // 存储请求头部字段键值对

    
public:
    int io_state_;                   // I/O状态: 0表示读状态，1表示写状态
    int improved_state_;                // 处理状态: 0表示未处理，1表示已处理
    int timer_flag_;                     // 定时器标志

    char* html_root_;                // html文件根目录
    char html_file_[FILENAME_LEN];   // 客户请求的目标文件的完整路径
    char* html_file_addr_;         // 目标文件被mmap到内存中的起始位置

    MYSQL *mysql_;                   // 数据库连接
    SQLConnectionPool *sql_conn_pool_; // 指向全局连接池，用于按需获取连接
    struct stat file_stat_;            // 目标文件的状态

    std::map<std::string, std::string> user_data_;      // 存储用户数据
    int user_count_;                                   // 用户数量
    MutexLock locker_;     // 线程锁

    bool is_keep_alive() const { return keep_alive_; }
    int get_sockfd() const { return sockfd_; }
};

#endif // HTTPREQUEST_HPP