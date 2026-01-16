#include "webserver.hpp"


// 构造函数
WebServer::WebServer() : 
            port_(8888), 
            linger_mode_(0),
            users_(nullptr),
            conn_thread_pool_(nullptr) {

    // html资源根目录
    char server_path[200];
    getcwd(server_path, 200);       // 获取当前工作目录
    char root[6] = "/html";
    html_root_ = (char*)malloc(strlen(server_path)+strlen(root)+1);
    strcpy(html_root_, server_path);
    strcat(html_root_, root);
    std::cout << "WebServer html_root_: " << html_root_ << std::endl;

    // 预分配 http 连接对象数组
    users_ = new http_reply[MAX_FD];
}

WebServer::~WebServer(){
    close(ep_fd_);
    close(listen_fd_);
    delete[] users_;
    delete conn_thread_pool_;
    free(html_root_);
}

void WebServer::init(int port, int linger_mode, int trig_mode, int actor_mode, int concurrent_mode,
                     int db_host, std::string db_user, std::string db_password, std::string db_name, int sql_num){
    port_ = port;
    linger_mode_ = linger_mode;
    trig_mode_ = trig_mode;
    actor_mode_ = actor_mode;
    concurrent_mode_ = concurrent_mode;

    // 数据库设置
    db_host_ = db_host;
    db_user_ = db_user;
    db_password_ = db_password;
    db_name_ = db_name;
    sql_num_ = sql_num;

}

// 设置数据库连接池
void WebServer::setSqlConnPool(){
    sql_conn_pool_ = SQLConnectionPool::GetInstance();
    sql_conn_pool_->init("localhost", db_user_, db_password_, db_name_, db_host_, sql_num_, close_log_);

    // 使用 users_[0] 来初始化用户数据（预加载到静态 map）
    users_[0].init_mysql(sql_conn_pool_);

    std::cout << "webserver user_data_: " << std::endl;
    for(auto &user: users_[0].user_data_){
        std::cout << "username: " << user.first << ", password: " << user.second << std::endl;
    }
}
// 设置网络连接线程池
void WebServer::setConnThreadPool(){
    conn_thread_pool_ = new ThreadPool<http_reply>(actor_mode_, concurrent_mode_, sql_conn_pool_, 8, 100);
}

void WebServer::eventListen(){
    // 创建监听套接字————IPV4, TCP协议
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd_ == -1){
        perror("socket creation failed");
        // TODO: 何时会创建socket失败，失败时应该重新创建直到成功
        return;
    }
    // socket选项设置, 使用SOL_SOCKET通用设置
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));    // 允许端口复用
    // 连接关闭行为设置
    struct linger so_linger;
    if(0 == linger_mode_){
        // close默认行为，close将立即返回，TCP模块负责把该socket对应的TCP发送缓冲区中残留的数据发送给对方。
        so_linger.l_onoff = 0;  // 关闭时不等待数据发送完毕
        so_linger.l_linger = 0; // 滞留时间为0
        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    }
    else if(1 == linger_mode_){
        //  close系统调用立即返回，TCP模块将丢弃被关闭的socket对应的TCP发送缓冲区中残留的数据，同时给对方发送一个复位报文段
        so_linger.l_onoff = 1; 
        so_linger.l_linger = 0; 
        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    }
    else{
        // close的行为取决于两个条件：一是被关闭的socket对应的TCP发送缓冲区中是否还有残留的数据；二是该socket是阻塞的，还是非阻塞的。
        so_linger.l_onoff = 1;  // 关闭时不等待数据发送完毕
        so_linger.l_linger = 1; // 滞留时间为0
        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    }

    // 设置监听ip地址和端口号
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;  // IPV4地址
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);    // 绑定到任意IP地址
    server_addr.sin_port = htons(port_);  // 端口号转换为网络字节序
    std::cout << "WebServer listening on port: " << port_ << std::endl;
    // 绑定监听套接字
    int ret = bind(listen_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(ret == -1){
        perror("bind failed");
        close(listen_fd_);
        return;
    }

    // 设置监听
    ret = listen(listen_fd_, 5);
    if(ret == -1){
        perror("listen failed");
        close(listen_fd_);
        return;
    }

    // epoll内核事件表创建
    // epoll_event events[MAX_EVENT_NUMBER];
    ep_fd_ = epoll_create(1024);
    if(ep_fd_ == -1){
        perror("epoll_create failed");
        // TODO: epoll_create创建失败的话需要进行什么处理
        exit(1);
        return;
    }

    util_epoll_.addFd(ep_fd_, listen_fd_, false, trig_mode_);

}

// 处理连接事件，初始化新连接
void WebServer::handleConnEvent(int sock_fd){
    // 初始化该连接的 http_reply 对象（继承自 http_request）
    users_[sock_fd].init(sock_fd, client_addr_, html_root_);
    users_[sock_fd].init_mysql(sql_conn_pool_);  // 设置连接池指针，便于按需获取 MySQL 连接
}

// 处理读事件
void WebServer::handleReadEvent(int sock_fd){
    // reactor 模式：I/O 由工作线程完成
    if(0 == actor_mode_){
        // 设置读状态，将任务加入线程池
        users_[sock_fd].io_state_ = 0;  // 0 表示读状态
        conn_thread_pool_->append(&users_[sock_fd]);
        std::cout << "[Reactor] 读事件加入线程池, fd=" << sock_fd << std::endl;
    }
    // proactor 模式：主线程完成 I/O，工作线程只处理业务逻辑
    else {
        if(users_[sock_fd].read_buffer()){     // 主线程读数据成功
            conn_thread_pool_->append(&users_[sock_fd]);
            std::cout << "[Proactor] 读取成功，任务加入线程池, fd=" << sock_fd << std::endl;
        } else {
            // 读取失败，关闭连接
            std::cerr << "[Proactor] 读取失败, fd=" << sock_fd << std::endl;
            util_epoll_.deleteFd(ep_fd_, sock_fd);
            close(sock_fd);
        }
    }
}

// 处理写事件
void WebServer::handleWriteEvent(int sock_fd){
    // reactor 模式
    if(0 == actor_mode_){
        // 设置写状态，将任务加入线程池
        users_[sock_fd].io_state_ = 1;  // 1 表示写状态
        conn_thread_pool_->append(&users_[sock_fd]);
        std::cout << "[Reactor] 写事件加入线程池, fd=" << sock_fd << std::endl;
    }
    // proactor 模式
    else {
        if(users_[sock_fd].write(sock_fd)){   
            std::cout << "[Proactor] 写数据成功, fd=" << sock_fd << std::endl;
            // 如果不保持长连接，关闭
            if(!users_[sock_fd].is_keep_alive()){
                util_epoll_.deleteFd(ep_fd_, sock_fd);
                close(sock_fd);
            }
        } else {
            std::cerr << "[Proactor] 写数据失败, fd=" << sock_fd << std::endl;
            util_epoll_.deleteFd(ep_fd_, sock_fd);
            close(sock_fd);
        }
    }
}

// 事件循环：通过线程池处理读写事件
void WebServer::testEventLoop(int epoll_fd, int listen_fd){
    int client_fd;  // 新连接的客户端描述符
    int sock_fd;    // 触发事件的客户端描述符
    
    while(1){
        int ready_fds = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
        if(ready_fds == -1){
            if(errno == EINTR) continue;  // 被信号中断，继续
            perror("epoll_wait error");
            return;
        }
        
        for(int i = 0; i < ready_fds; i++){
            const epoll_event& event = events[i];
            const int event_fd = event.data.fd;
            
            // 1. 处理新连接
            if(event_fd == listen_fd){
                socklen_t cli_len = sizeof(client_addr_);
                client_fd = accept(listen_fd, (struct sockaddr*)&client_addr_, &cli_len);
                if(client_fd < 0){
                    perror("accept error");
                    continue;
                }
                if(client_fd >= MAX_FD){
                    std::cerr << "连接数超过最大限制" << std::endl;
                    close(client_fd);
                    continue;
                }
                // 初始化该连接的 http 对象
                handleConnEvent(client_fd);
                // 将新连接加入 epoll 监听（监听读事件）
                util_epoll_.addFd(ep_fd_, client_fd, true, trig_mode_);  // oneshot=true
                std::cout << "新连接建立, fd=" << client_fd << std::endl;
            }
            // 2. 处理错误事件
            else if(event.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                // 客户端关闭连接或出错
                std::cout << "连接关闭或错误, fd=" << event_fd << std::endl;
                util_epoll_.deleteFd(ep_fd_, event_fd);
                close(event_fd);
            }
            // 3. 处理读事件
            else if(event.events & EPOLLIN){
                sock_fd = event_fd;
                handleReadEvent(sock_fd);
            }
            // 4. 处理写事件
            else if(event.events & EPOLLOUT){
                sock_fd = event_fd;
                handleWriteEvent(sock_fd);
            }
        }
    }
}