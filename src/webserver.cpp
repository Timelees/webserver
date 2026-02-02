#include "webserver.hpp"

// 构造函数
WebServer::WebServer() : port_(8888),
                         linger_mode_(0),
                         users_(nullptr),
                         conn_thread_pool_(nullptr)
{

    // html资源根目录
    char server_path[200];
    getcwd(server_path, 200); // 获取当前工作目录
    char root[6] = "/html";
    html_root_ = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(html_root_, server_path);
    strcat(html_root_, root);
    std::cout << "WebServer html_root_: " << html_root_ << std::endl;

    // 预分配 http 连接对象数组
    users_ = new http_reply[MAX_FD];

    // 客户端定时器管理
    users_data_ = new client_data[MAX_FD];
    timer_manager_ = new conn_timer_manager(TIMESLOT);
    WebServer::instance = this;

    // 活跃连接数（由 WebServer 统一维护）
    conn_count_ = 0;
}

WebServer::~WebServer()
{
    close(ep_fd_);
    close(listen_fd_);
    close(pipe_fd_[0]);
    close(pipe_fd_[1]);
    delete[] users_;
    delete[] users_data_;
    delete conn_thread_pool_;
    delete timer_manager_;
    free(html_root_);
}

void WebServer::init(int port, int linger_mode, int trig_mode, int actor_mode, int concurrent_mode,
                     int db_host, std::string db_user, std::string db_password, std::string db_name, int sql_num)
{
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

// 触发模式设置
void WebServer::trigMode()
{
    /*采用LT工作模式的文件描述符，当epoll_wait检测到其上有事件发生并将此事件通知应用程序后，应用程序可以不立即处理该事件。这样，当应用程序下一次调用epoll_wait时，
    epoll_wait还会再次向应用程序通告此事件，直到该事件被处理。
    而对于采用ET工作模式的文件描述符，当epoll_wait检测到其上有事件发生并将此事件通知应用程序后，应用程序必须立即处理该事件，因为后续的epoll_wait调用将
    不再向应用程序通知这一事件。*/
    // LT + LT
    if (0 == trig_mode_)
    {
        listen_trig_mode_ = 0;
        conn_trig_mode_ = 0;
    }
    // LT + ET
    else if (1 == trig_mode_)
    {
        listen_trig_mode_ = 0;
        conn_trig_mode_ = 1;
    }
    // ET +LT
    else if (2 == trig_mode_)
    {
        listen_trig_mode_ = 1;
        conn_trig_mode_ = 0;
    }
    // ET + ET
    else if (3 == trig_mode_)
    {
        listen_trig_mode_ = 1;
        conn_trig_mode_ = 1;
    }
    std::cout << "===============触发模式配置=============" << std::endl;
    std::cout << "listen触发模式：" << (listen_trig_mode_ ? "ET" : "LT") << std::endl;
    std::cout << "connect触发模式：" << (conn_trig_mode_ ? "ET" : "LT") << std::endl;

}

// 设置数据库连接池
void WebServer::setSqlConnPool()
{
    sql_conn_pool_ = SQLConnectionPool::GetInstance();
    sql_conn_pool_->init("localhost", db_user_, db_password_, db_name_, db_host_, sql_num_, close_log_);

    // 使用 users_[0] 来初始化用户数据（预加载到静态 map）
    users_[0].init_mysql(sql_conn_pool_);

    std::cout << "=================数据库连接池配置===========================" << std::endl;
    std::cout << "webserver user_data_(使用users_[0]存储数据库信息): " << std::endl;
    for (auto &user : users_[0].user_data_)
    {
        std::cout << "username: " << user.first << ", password: " << user.second << std::endl;
    }
    std::cout << "==========================================================" << std::endl;
}
// 设置网络连接线程池
void WebServer::setConnThreadPool()
{
    if (concurrent_mode_ == 0)
    {
        // 半同步/半异步模型
        conn_thread_pool_ = new ThreadPool<http_reply>(actor_mode_, concurrent_mode_, sql_conn_pool_, nullptr, 8, 100);
    }
    else
    {
        // 领导者/跟随者模型
        handle_set_ = new HandleSet(MAX_EVENT_NUMBER);
        conn_thread_pool_ = new ThreadPool<http_reply>(actor_mode_, concurrent_mode_, sql_conn_pool_, handle_set_, 8, 100);
    }
}

// 设置客户端定时器
void WebServer::setClientTimer(int conn_fd, struct sockaddr_in client_addr)
{
    users_data_[conn_fd].sockfd = conn_fd;      // 保存客户端socket文件描述符
    users_data_[conn_fd].address = client_addr; // 保存客户端地址
    util_timer *timer = new util_timer;
    timer->cb_func_ = WebServer::timer_bridge;
    timer->user_data_ = &users_data_[conn_fd];
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;  // 设置超时时间为3个时间槽
    users_data_[conn_fd].timers = timer; // 保存客户端对应的定时器，超时则触发回调cb_func关闭连接
    std::cout << "[WebServer::setClientTimer] add timer fd=" << conn_fd << " expire=" << timer->expire << " now=" << cur << std::endl;
    if (timer_manager_)
        timer_manager_->add_timer(timer);
    // 测试部分，打印当前的定时器列表
    std::cout << "[WebServer::setClientTimer] current timer_manager " << std::endl;
    timer_manager_->print_timer();
}

void WebServer::cb_func(client_data *user_data)
{
    // 处理超时事件
    const int fd = user_data->sockfd;
    std::cout << "[WebServer::cb_func Timer] client fd=" << fd << " time out, closing..." << std::endl;

    // 从内核事件表中删除该连接
    util_epoll_.deleteFd(ep_fd_, fd);
    close(fd);

    // 清理定时器指针（真实释放由 timer_manager_->del_timer 负责）
    user_data->timers = nullptr;

    // 连接资源数减一（下限保护）
    if (conn_count_ > 0)
        conn_count_--;
    std::cout << "[WebServer::cb_func] active=" << conn_count_ << " (after timeout close)" << std::endl;
}

// static bridge
void WebServer::timer_bridge(client_data *user_data)
{
    if (WebServer::instance)
        WebServer::instance->cb_func(user_data);
}

// define static instance
WebServer *WebServer::instance = nullptr;

// signal handler bridge to forward C-style signals to the util_epoll_ instance method
static void signal_handler_bridge(int sig)
{
    if (WebServer::instance)
        WebServer::instance->util_epoll_.signalHandler(sig);
}

void WebServer::eventListen()
{
    // 创建监听套接字————IPV4, TCP协议
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1)
    {
        perror("socket creation failed");
        // TODO: 何时会创建socket失败，失败时应该重新创建直到成功
        return;
    }
    // socket选项设置, 使用SOL_SOCKET通用设置
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // 允许端口复用
    // 连接关闭行为设置
    struct linger so_linger;
    if (0 == linger_mode_)
    {
        // close默认行为，close将立即返回，TCP模块负责把该socket对应的TCP发送缓冲区中残留的数据发送给对方。
        so_linger.l_onoff = 0;  // 关闭时不等待数据发送完毕
        so_linger.l_linger = 0; // 滞留时间为0
        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    }
    else if (1 == linger_mode_)
    {
        //  close系统调用立即返回，TCP模块将丢弃被关闭的socket对应的TCP发送缓冲区中残留的数据，同时给对方发送一个复位报文段
        so_linger.l_onoff = 1;
        so_linger.l_linger = 0;
        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    }
    else
    {
        // close的行为取决于两个条件：一是被关闭的socket对应的TCP发送缓冲区中是否还有残留的数据；二是该socket是阻塞的，还是非阻塞的。
        so_linger.l_onoff = 1;  // 关闭时不等待数据发送完毕
        so_linger.l_linger = 1; // 滞留时间为0
        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    }

    // 设置监听ip地址和端口号
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;                // IPV4地址
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 绑定到任意IP地址
    server_addr.sin_port = htons(port_);             // 端口号转换为网络字节序
    std::cout << "[WebServer::eventListen] listen_fd = " << listen_fd_ << std::endl; 
    std::cout << "WebServer listening on port: " << port_ << std::endl;
    // 绑定监听套接字
    int ret = bind(listen_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1)
    {
        perror("bind failed");
        close(listen_fd_);
        return;
    }

    // 设置监听
    ret = listen(listen_fd_, 5);
    if (ret == -1)
    {
        perror("listen failed");
        close(listen_fd_);
        return;
    }

    // epoll内核事件表创建
    epoll_event events[MAX_EVENT_NUMBER];
    ep_fd_ = epoll_create(1024);
    std::cout << "[WebServer::eventListen] epoll_fd = " << ep_fd_ << std::endl; 
    if (ep_fd_ == -1)
    {
        perror("epoll_create failed");
        // TODO: epoll_create创建失败的话需要进行什么处理
        exit(1);
        return;
    }

    util_epoll_.addFd(ep_fd_, listen_fd_, false, listen_trig_mode_);

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipe_fd_);
    if(ret == -1){
        perror("socketpair failed");
        return;
    }
    util_epoll_.setNonBlocking(pipe_fd_[1]);
    util_epoll_.addFd(ep_fd_, pipe_fd_[0], false, 0);   // pipe_fd_[0]读端注册到epoll事件表上，LT触发

    util_epoll_.addSignal(SIGPIPE, SIG_IGN, false);
    util_epoll_.addSignal(SIGALRM, signal_handler_bridge, false);
    util_epoll_.addSignal(SIGTERM, signal_handler_bridge, false);

    alarm(TIMESLOT);

    util_epoll_.util_ep_fd_ = ep_fd_;
    util_epoll_.util_pipe_fd_ = pipe_fd_;
   
}


// 调整连接对应的定时器的超时时间
void WebServer::adjust_timer(int sock_fd, util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    timer_manager_->resort_timer(timer);

    std::cout << "[WebServer::adjust_timer] 客户端(fd =" << sock_fd << ")有数据传输--（活跃连接），延长定时器超时时间" << std::endl;
    std::cout << "调整后的定时器列表：" << std::endl;
    timer_manager_->print_timer();
}

// 移除指定连接的定时器
void WebServer::delete_timer(int sock_fd, util_timer *timer)
{
    if (!timer)
        return;

    // 处理定时器
    timer_manager_->del_timer(timer);
    if (users_data_[sock_fd].timers == timer)
    {
        users_data_[sock_fd].timers = nullptr;
    }
    delete timer;
    std::cout << "[WebServer::delete_timer] remove timer fd=" << sock_fd << std::endl;
    std::cout << "移除后的定时器列表：" << std::endl;
    timer_manager_->print_timer();
}

// 处理连接事件，初始化新连接
void WebServer::handleConnEvent(int sock_fd)
{
    // 初始化该连接的 http_reply 对象（继承自 http_request）
    users_[sock_fd].init(sock_fd, client_addr_, ep_fd_, trig_mode_, html_root_);
    users_[sock_fd].init_mysql(sql_conn_pool_); // 设置连接池指针，便于按需获取 MySQL 连接

    std::cout << "[WebServer::handleConnEvent] Init Event ( " << sock_fd << " ) 到users_[ " << sock_fd << " ]" << std::endl;
}

// 处理读事件
void WebServer::handleReadEvent(int sock_fd)
{
    std::cout << "[WebServer::handleReadEvent] 处理读事件(sockfd = " << sock_fd << ")" << std::endl;
    util_timer *timer = users_data_[sock_fd].timers;

    // reactor 模式：I/O 由工作线程完成
    if (0 == actor_mode_)
    {
        if (timer)
        {
            adjust_timer(sock_fd, timer); // 调整读事件连接的定时器超时时长
        }

        // 设置读状态，将任务加入线程池
        users_[sock_fd].io_state_ = 0; // 0 表示读状态
        conn_thread_pool_->append(&users_[sock_fd]);
        std::cout << "[WebServer::handleReadEvent--Reactor] 读事件加入线程池, fd=" << sock_fd << std::endl;

        // 主线程需要等工作线程完成后才能知道连接的最终状态（比如是否需要关闭、是否需要移除定时器）
        // 等待工作线程把 users[sockfd].improv 设为 1”作为完成信号，然后由主线程去处理 timer_flag（关闭连接等）并重置标志
        while (true)
        {
            if (users_[sock_fd].improved_state_ == 1)
            { // 当前事件已处理
                if (users_[sock_fd].timer_flag_ == 1)
                { // 响应失败时，定时器标志为1
                    delete_timer(sock_fd, users_data_[sock_fd].timers);
                    users_[sock_fd].timer_flag_ = 0;
                }
                users_[sock_fd].improved_state_ = 0;
                break;
            }
        }
    }
    // proactor 模式：主线程完成 I/O，工作线程只处理业务逻辑
    else
    {
        if (users_[sock_fd].read_buffer())
        { // 主线程读数据成功
            conn_thread_pool_->append(&users_[sock_fd]);
            std::cout << "[WebServer::handleReadEvent--Proactor] 读取成功，任务加入线程池, fd=" << sock_fd << std::endl;

            if (timer)
            {
                adjust_timer(sock_fd, timer);
            }
            // 主线程已经完成了 I/O（read_once / write），只把后续的业务处理交给工作线程
            // 连接的 I/O 状态和定时器已经由主线程处理好，因此不需要等待工作线程返回再决定是否关闭/调整定时器
        }
        else
        {
            // 读取失败，关闭连接
            std::cerr << "[WebServer::handleReadEvent--Proactor] 读取失败, fd=" << sock_fd << std::endl;
            // util_epoll_.deleteFd(ep_fd_, sock_fd);
            // close(sock_fd);
            delete_timer(sock_fd, timer); // 移除定时器的同时，会关闭相关连接并移除epoll内核事件表上的事件
        }
    }
}

// 处理写事件
void WebServer::handleWriteEvent(int sock_fd)
{
    util_timer *timer = users_data_[sock_fd].timers;
    // reactor 模式
    if (0 == actor_mode_)
    {

        // 设置写状态，将任务加入线程池
        users_[sock_fd].io_state_ = 1; // 1 表示写状态
        conn_thread_pool_->append(&users_[sock_fd]);
        std::cout << "[WebServer::handleReadEvent--Reactor] 写事件加入线程池, fd=" << sock_fd << std::endl;

        while (true)
        {
            if (users_[sock_fd].improved_state_ == 1)
            { // 已处理
                if (users_[sock_fd].timer_flag_ == 1)
                {
                    delete_timer(sock_fd, timer);
                    users_[sock_fd].timer_flag_ = 0;
                }
                users_[sock_fd].improved_state_ = 0;
                break;
            }
        }
    }
    // proactor 模式
    else
    {
        if (users_[sock_fd].write(sock_fd))
        {
            std::cout << "[WebServer::handleReadEvent--Proactor] 写数据成功, fd=" << sock_fd << std::endl;
            if (timer)
            {
                adjust_timer(sock_fd, timer);
            }
        }
        else
        {
            std::cerr << "[WebServer::handleReadEvent--Proactor] 写数据失败, fd=" << sock_fd << std::endl;
            delete_timer(sock_fd, timer);
        }
    }
}

// 接收新的客户端连接
bool WebServer::acceptConnections()
{
    struct sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);
    if (listen_trig_mode_ == 0)
    { // LT模式的监听连接
        int connfd = accept(listen_fd_, (struct sockaddr *)&client_address, &client_addr_len);
        if (connfd < 0)
        {
            perror("accept error");
            return false;
        }
        std::cout << "[Webserver-acceptConnections] 当前客户端连接数： " << conn_count_ << std::endl;
        if (conn_count_ >= MAX_FD)
        {
            std::cerr << "连接数超过最大限制--" << MAX_FD << std::endl;
            return false;
        }
        // 初始化该连接的 http 对象
        handleConnEvent(connfd);


        std::cout << "[Webserver-acceptConnections] 新连接建立, fd=" << connfd << std::endl;

        // 设置当前连接的定时器
        setClientTimer(connfd, client_address);

        conn_count_++;
        std::cout << "[WebServer::cb_func] active=" << conn_count_ << " (after accept)" << std::endl;
    }
    else
    { // ET模式的监听连接
        while (1)
        {
            int connfd = accept(listen_fd_, (struct sockaddr *)&client_address, &client_addr_len);
            if (connfd < 0)
            {
                perror("accept error");
                return false;
            }
            std::cout << "[Webserver-acceptConnections] 当前客户端连接数： " << conn_count_ << std::endl;
            if (conn_count_ >= MAX_FD)
            {
                std::cerr << "连接数超过最大限制--" << MAX_FD << std::endl;
                return false;
            }
            // 初始化该连接的 http 对象
            handleConnEvent(connfd);

            std::cout << "[Webserver-acceptConnections] 新连接建立, fd=" << connfd << " (epoll registered in init)" << std::endl;

            // 设置当前连接的定时器
            setClientTimer(connfd, client_address);

            conn_count_++;
            std::cout << "[Webserver-acceptConnections] 当前激活的连接数 active=" << conn_count_ << " (after accept)" << std::endl;
        }
        return false;
    }
    return true;
}

// 处理信号
bool WebServer::dealWithSignal(bool &time_out, bool &stop_server){
    int ret = 0, sig;
    char signals[1024];
    ret = recv(pipe_fd_[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                time_out = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

// 事件循环
void WebServer::eventLoop()
{
    while (!stop_server_)
    {
        if (concurrent_mode_ == 0)
        { // 半同步/半异步模型：主线程做epoll_wait, 工作线程处理I/O
            int ready_fds = epoll_wait(ep_fd_, events, MAX_EVENT_NUMBER, -1);
            std::cout << "[WebServer::eventLoop] 就绪的事件数ready_fds = " << ready_fds << std::endl;
            if (ready_fds == -1)
            {
                if (errno == EINTR)
                    continue; // 被信号中断，继续
                perror("epoll_wait error");
                return;
            }

            for (int i = 0; i < ready_fds; i++)
            {
                int sockfd = events[i].data.fd;

                // 检测并接收新的客户端连接
                if (sockfd == listen_fd_)
                {
                    std::cout << "[WebServer::eventLoop] 新的客户端连接sockfd = " << sockfd << std::endl;
                    bool flag = acceptConnections(); // 接收新连接
                    if (!flag)
                    {
                        std::cout << "[WebServer::eventLoop] acceptConnections failed for listen_fd=" << listen_fd_ << std::endl;
                        continue; // accept失败，说明不是新的连接，应该处理客户端事件的数据
                    }
                }
                else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                {
                    // 客户端关闭连接或出错
                    std::cout << "[WebServer::eventLoop] 连接关闭或错误, fd=" << sockfd << std::endl;
                    util_epoll_.deleteFd(ep_fd_, sockfd);
                    close(sockfd);
                    if (users_data_[sockfd].timers)
                    {
                        timer_manager_->del_timer(users_data_[sockfd].timers); // 移除该sockfd对应的资源定时器
                        users_data_[sockfd].timers = nullptr;
                    }
                    if (conn_count_ > 0)
                        conn_count_--;
                    std::cout << "[WebServer::eventLoop] active=" << conn_count_ << " (after error close)" << std::endl;
                }
                else if ((sockfd == pipe_fd_[0]) && (events[i].events & EPOLLIN)){
                    bool flag = dealWithSignal(time_out_, stop_server_);
                    if(false == flag){
                        std::cout << "[WebServer::eventLoop] 处理客户端连接失败" << std::endl;
                    }
                }
                else if (events[i].events & EPOLLIN)
                {
                    handleReadEvent(sockfd);
                }
                else if (events[i].events & EPOLLOUT){
                    handleWriteEvent(sockfd);
                }
            }

   
            if (time_out_ )
            {   
                std::cout << "[WebServer::eventLoop] 定期清除超时的定时器" << std::endl;
                if (timer_manager_)
                {
                    timer_manager_->timer_handler();
                    timer_manager_->print_timer();
                }
                time_out_ = false;
            }
        }
        else
        {
            // 领导者/跟随者模型
            while (true)
            {
            }
        }
    }
}

// 事件循环：通过线程池处理读写事件
void WebServer::testEventLoop(int epoll_fd, int listen_fd)
{
    int client_fd; // 新连接的客户端描述符
    int sock_fd;   // 触发事件的客户端描述符

    if (concurrent_mode_ == 0)
    {
        // 半同步/半异步模型：主线程做epoll_wait, 工作线程处理I/O
        while (1)
        {
            int ready_fds = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
            if (ready_fds == -1)
            {
                if (errno == EINTR)
                    continue; // 被信号中断，继续
                perror("epoll_wait error");
                return;
            }

            for (int i = 0; i < ready_fds; i++)
            {
                const epoll_event &event = events[i];
                const int event_fd = event.data.fd;

                // 1. 处理新连接
                if (event_fd == listen_fd)
                {
                    socklen_t cli_len = sizeof(client_addr_);
                    client_fd = accept(listen_fd, (struct sockaddr *)&client_addr_, &cli_len);
                    if (client_fd < 0)
                    {
                        perror("accept error");
                        continue;
                    }
                    if (client_fd >= MAX_FD)
                    {
                        std::cerr << "连接数超过最大限制" << std::endl;
                        close(client_fd);
                        continue;
                    }
                    // 初始化该连接的 http 对象
                    handleConnEvent(client_fd);
                    // 将新连接加入 epoll 监听（监听读事件）
                    util_epoll_.addFd(ep_fd_, client_fd, true, trig_mode_); // oneshot=true
                    std::cout << "新连接建立, fd=" << client_fd << std::endl;
                }
                // 2. 处理错误事件
                else if (event.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                {
                    // 客户端关闭连接或出错
                    std::cout << "连接关闭或错误, fd=" << event_fd << std::endl;
                    util_epoll_.deleteFd(ep_fd_, event_fd);
                    close(event_fd);
                }
                // 3. 处理读事件
                else if (event.events & EPOLLIN)
                {
                    sock_fd = event_fd;
                    handleReadEvent(sock_fd);
                }
                // 4. 处理写事件
                else if (event.events & EPOLLOUT)
                {
                    sock_fd = event_fd;
                    handleWriteEvent(sock_fd);
                }
            }
        }
    }
    else
    { // 领导者/跟随者模型
        while (true)
        {
        }
    }
}