#include "http/http_reply.hpp"
#include <cstdarg>

void http_reply::init_reply(){
    write_idx_ = 0;
    keep_alive_ = false;
    rep_file_stat_ = file_stat_;            // 从request中获得的文件状态
    rep_html_file_addr_ = html_file_addr_;  // 从request中获得的map映射的html文件地址
    iov_cnt_ = 0;
    bytes_to_send_ = 0;
    memset(write_buf_, '\0', WRITE_BUFFER_SIZE);
}

bool http_reply::add_response(const char* format, ...){
    if(write_idx_ >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    // 将参数format指向内容写到缓冲区中
    int len = vsnprintf(write_buf_ + write_idx_, WRITE_BUFFER_SIZE - 1 - write_idx_, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 -write_idx_)){
        va_end(arg_list);
        return false;
    }
    write_idx_ += len;  // 更新写缓冲区指针指向
    va_end(arg_list);

    return true;
}

// 添加响应状态：如200 OK， 404 Not Found
bool http_reply::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加响应头部
bool http_reply::add_headers(int content_len){
    return add_content_lengtn(content_len) && add_linger() && add_blank_line();
}

// 添加响应正文
bool http_reply::add_content(const char* content){
    return add_response("%s", content);
}

// 添加响应正文类型
bool http_reply::add_content_type(){
    return add_response("Content-Type: %s; charset=UTF-8\r\n", "text/html");
}

// 添加响应正文长度
bool http_reply::add_content_lengtn(int content_length){
    return add_response("Content-Length:%d\r\n", content_length);
}   

// 添加是否保持连接
bool http_reply::add_linger(){
    return add_response("Connection:%s\r\n", (keep_alive_ == true) ? "keep-alive" : "close");
}   

// 添加空行
bool http_reply::add_blank_line(){
    return add_response("%s", "\r\n");
}

// 生成响应内容
bool http_reply::process_reply(HTTP_CODE ret){
    switch(ret)
    {
        
        case INTERNAL_ERROR:    // 服务器内部错误
        {
            add_status_line(500, rep_status.error_500_title);
            add_headers(strlen(rep_status.error_500_form));
            if(!add_content(rep_status.error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:       // HTTP请求有语法错误等
        {
            add_status_line(400, rep_status.error_400_title);
            add_headers(strlen(rep_status.error_400_form));
            if(!add_content(rep_status.error_400_form)){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:     // 访问权限不够
        {
            add_status_line(403, rep_status.error_403_title);
            add_headers(strlen(rep_status.error_403_form));
            if(!add_content(rep_status.error_403_form)){
                return false;
            }
            break;
        }
        case NO_RESOURCE:       // 请求资源不存在
        {   
            add_status_line(404, rep_status.error_404_title);
            add_headers(strlen(rep_status.error_404_form));
            if(!add_content(rep_status.error_404_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:      // 文件请求
        {
            add_status_line(200, rep_status.ok_200_title);
            if(rep_file_stat_.st_size != 0){    
                add_headers(rep_file_stat_.st_size);
                iov[0].iov_base = write_buf_;
                iov[0].iov_len = write_idx_;
                iov[1].iov_base = rep_html_file_addr_;
                iov[1].iov_len = rep_file_stat_.st_size;
                iov_cnt_ = 2;
                bytes_to_send_ = write_idx_ + rep_file_stat_.st_size;
                return true;
            }
            else{   // 文件内容为空
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)){
                    return false;
                }
            }

        }
        default:
            return false;
    }
    // 非文件请求响应时，io向量仅指向写缓冲区
    iov[0].iov_base = write_buf_;
    iov[0].iov_len = write_idx_;
    iov_cnt_ = 1;
    bytes_to_send_ = write_idx_;
    return true;
}

// 发送数据到连接的sockfd, 处理写
bool http_reply::write(int sockfd){
    int iovcnt = iov_cnt_;
    struct iovec tmp_iov[2];
    tmp_iov[0] = iov[0];
    tmp_iov[1] = iov[1];
    long long bytes_left = bytes_to_send_;      // 写缓冲区剩余字节数
    while(bytes_left > 0){
        ssize_t n = writev(sockfd, tmp_iov, iovcnt);        // 将写缓冲区的数据写入socket
        if(n < 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // 资源暂时不可用，稍后重试
                return true;
            }
            return false;
        }
        bytes_left -= n;
        // 调整tmp_iov中的写缓冲区的内容
        ssize_t tmp_idx = n;        // 更新数据指针到已写的数据后面
        for(int i = 0; i < iovcnt; ++i){
            if(tmp_idx < tmp_iov[i].iov_len){       // 索引小于写缓冲区数据长度，则说明该iov还未写完
                tmp_iov[i].iov_base = (char*)tmp_iov[i].iov_base + tmp_idx;    // 更新数据指针到已写的数据后面
                tmp_iov[i].iov_len -= tmp_idx;      // 减去已写入的字节数
                break;
            }
            // 写缓冲区的数据已经写完
            tmp_idx -= tmp_iov[i].iov_len;
            tmp_iov[i].iov_base = (char*)tmp_iov[i].iov_base + tmp_iov[i].iov_len;
            tmp_iov[i].iov_len = 0;
        }
        // 如果第一个iov长度为0, 把后面的向前移动
        while(iovcnt > 0 && tmp_iov[0].iov_len == 0){
            if(iovcnt == 1) break;
            tmp_iov[0] = tmp_iov[1];
            iovcnt = 1;
        }
    }
    return true;  // 发送完成
}



