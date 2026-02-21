#include "webserver.hpp"
#include "utils/config.hpp"
#include <string>
int main(int  argc, char *argv[]){
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    // 数据库设置
    int db_host_ = config.port_;
    std::string db_user_ = config.db_user_;
    std::string db_password_ = config.db_password_;
    std::string db_name_ = config.db_name_;
    int sql_num_ = config.sql_num_;

    int port_ = config.port_;
    int linger_mode_ = config.linger_mode_;
    int trig_mode_ = config.trig_mode_;
    int actor_mode_ = config.actor_mode_;
    int concurrent_mode_ = config.concurrent_mode_;
    int close_log_ = config.close_log_;
    int log_write_ = config.log_write_;
    int thread_num_ = config.thread_num_;
    
    // 初始化服务器
    server.init(port_, linger_mode_, trig_mode_, actor_mode_, concurrent_mode_,
                db_host_, db_user_, db_password_, db_name_, sql_num_, close_log_, log_write_, thread_num_);

    // 日志初始化
    server.setLog();

    // 数据库初始化
    server.setSqlConnPool();

    // 设置网络连接线程池
    server.setConnThreadPool();

    // 触发模式设置
    server.trigMode();

    // 设置服务器监听事件，并将监听事件加入到内核事件表
    server.eventListen();

    server.eventLoop();


    // server.testEventLoop(server.ep_fd_, server.listen_fd_);

    return 0;
}