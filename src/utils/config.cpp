#include "utils/config.hpp"

Config::Config(){

}

Config::~Config(){

}

void Config::parse_arg(int argc, char *argv[]){
    int opt;
    const char* opt_strings = "p:l:t:a:m:c:w:s:x:";
    while ((opt = getopt(argc, argv, opt_strings)) != -1) {
        switch (opt) {
            case 'p':   // 端口号
                port_ = atoi(optarg);
                break;
            case 'l':   // 连接关闭模式
                linger_mode_ = atoi(optarg);
                break;
            case 't':   // 触发模式
                trig_mode_ = atoi(optarg);
                break;
            case 'a':   // 事件模型
                actor_mode_ = atoi(optarg);
                break;
            case 'm':   // 并发模型
                concurrent_mode_ = atoi(optarg);
                break;
            case 'c':   // 关闭日志
                close_log_ = atoi(optarg);
                break;
            case 'w':   // 日志写入方式
                log_write_ = atoi(optarg);
                break;
            case 's':   // 数据库连接池数量
                sql_num_ = atoi(optarg);
                break;
            case 'x':   // 线程池线程数量
                thread_num_ = atoi(optarg);
                break;
            default:
                break;
        }
    }
}
