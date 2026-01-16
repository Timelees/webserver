#ifndef HTTP_MESSAGE_HPP
#define HTTP_MESSAGE_HPP

class http_message{
public:
    http_message(){}
    ~http_message(){}
    enum METHOD{
        GET = 0,    // 申请获取资源
        HEAD,       // 仅获取资源的响应头
        POST,       // 提交数据给服务器
        PUT,        // 上传资源，客户端向服务器传送的数据取代指定的文档内容
        DELETE,     // 删除指定的资源
        TRACE,      // 返回原始HTTP请求的内容，主要用于测试或诊断
        OPTIONS,    // 返回服务器针对特定资源所支持的HTTP请求方法
        CONNECT,    // 将请求连接转换为透明的TCP/IP隧道，通常用于SSL加密的连接
        PATCH       // 对资源进行部分修改
    };
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0, // 当前正在分析请求行
        CHECK_STATE_HEADER,           // 当前正在分析头部字段
        CHECK_STATE_CONTENT           // 当前正在解析请求体
    };
    enum HTTP_CODE{
        NO_REQUEST,         // 请求不完整，需要继续读取客户数据
        GET_REQUEST,        // 获得了完整的HTTP请求
        BAD_REQUEST,        // HTTP请求有语法错误
        NO_RESOURCE,        // 请求资源不存在
        FORBIDDEN_REQUEST,  // 客户对资源没有足够的访问权限
        FILE_REQUEST,       // 文件请求，获取文件成功
        INTERNAL_ERROR,     // 服务器内部错误
        CLOSED_CONNECTION   // 客户端已经关闭连接
    };
    enum LINE_STATUS{
        LINE_OK = 0,    // 读取到一个完整的行
        LINE_BAD,       // 行出错
        LINE_OPEN       // 行数据尚且不完整
    };
    typedef struct REPLY_STATUS{
        const char *ok_200_title = "OK";
        const char *error_400_title = "Bad Request";
        const char *error_400_form = "请求异常（400）————语法错误或服务器无法处理\n";
        const char *error_403_title = "Forbidden";
        const char *error_403_form = "权限缺失（403）————用户权限不足\n";
        const char *error_404_title = "Not Found";
        const char *error_404_form = "页面不存在（404）————网页不存在\n";
        const char *error_500_title = "Internal Error";
        const char *error_500_form = "服务器内部错误（500）————请求文件时出现异常问题\n";
    }reply_status;


    static const int READ_BUFFER_SIZE = 2048;    // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;   // 写缓冲区大小
    static const int FILENAME_LEN = 200;         // html文件名最大长度
};

#endif // HTTP_MESSAGE_HPP