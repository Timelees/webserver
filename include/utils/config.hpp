#ifndef CONFIG_H
#define CONFIG_H

#include <unistd.h>
#include "../webserver.hpp"

class Config
{
public:
    Config();
    ~Config();

    void parse_arg(int argc, char *argv[]);

    // 数据库配置参数
    std::string db_user_ = "lee";
    std::string db_password_ = "123";
    std::string db_name_ = "webserverDB";
    

    // 服务器可修改配置参数
    int port_ = 8888;            // 端口号，              默认8888
    int linger_mode_ = 0;        // 连接关闭模式，         默认0：关闭连接时不延迟，1：延迟关闭
    int trig_mode_ = 0;          // 触发组合模式,          默认0: listenfd LT + connfd LT
    int listen_trig_mode_ = 0;   // listenfd触发模式，     默认0：LT， 1：ET
    int conn_trig_mode_ = 0;     // connfd触发模式，       默认0：LT， 1：ET
    int actor_mode_ = 1;         // 事件模型，              默认1：Proactor，0：Reactor
    int concurrent_mode_ = 0;    // 并发模型，              默认0：半同步/半异步模型，1：领导者/跟随者模型
    int close_log_ = 0;          // 关闭日志，              默认0：不关闭，1：关闭
    int log_write_ = 0;          // 日志写入方式，          默认0：同步，1：异步
    int sql_num_ = 8;            // 数据库连接池数量，      默认8
    int thread_num_ = 8;         // 线程池内的线程数量，    默认8
};

#endif // CONFIG_H