#include "webserver.hpp"
#include <string>
int main(int  argc, char *argv[]){
    // TODO: 从配置文件加载
    // 数据库设置
    int db_host_ = 8888;
    std::string db_user_ = "lee";
    std::string db_password_ = "123";
    std::string db_name_ = "webserverDB";
    int sql_num_ = 8;      // 数据库连接池数量

    // 初始化服务器
    WebServer server;
    
    // TODO：从配置文件加载
    int port_ = 8888;            // 端口
    int linger_mode_ = 0;       // 连接模型
    int trig_mode_ = 1;         // ET模式
    int actor_mode_ = 1;        // 事件模型，Proactor：1，Reactor：0
    int concurrent_mode_ = 0;   // 并发模型，0：半同步/半异步模型，1：领导者/跟随者模型

    db_host_ = port_;

    server.init(port_, linger_mode_, trig_mode_, actor_mode_, concurrent_mode_,
                db_host_, db_user_, db_password_, db_name_, sql_num_);

    // 数据库初始化
    server.setSqlConnPool();

    // 设置网络连接线程池
    server.setConnThreadPool();

    // 设置服务器监听事件，并将监听事件加入到内核事件表
    server.eventListen();

    server.testEventLoop(server.ep_fd_, server.listen_fd_);

    return 0;
}