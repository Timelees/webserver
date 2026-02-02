#include "http/http_request.hpp"
#include <unordered_map>
#include <map>

// 初始化客户端连接
void http_request::init(int sockfd, const sockaddr_in &addr, int epoll_fd, int trig_mode, char *root)
{
    sockfd_ = sockfd;
    addr_ = addr;
    html_root_ = root;
    epoll_fd_ = epoll_fd;
    trig_mode_ = trig_mode;
    util_epoll_.addFd(epoll_fd_, sockfd, false, trig_mode_);
    // 初始化http相关参数
    init();
}

// 初始化参数
void http_request::init()
{
    check_state_ = CHECK_STATE_REQUESTLINE; // 主状态机初始状态为检查请求行
    method_ = GET;
    url_ = NULL;
    version_ = NULL;
    content_ = NULL;
    content_length_ = 0;
    host_ = NULL;
    keep_alive_ = false;

    read_idx_ = 0;
    checked_idx_ = 0;
    start_line_ = 0;
    mysql_ = NULL;
    // 初始化缓冲区
    memset(read_buf_, '\0', READ_BUFFER_SIZE);

    memset(html_file_, '\0', FILENAME_LEN);
}

// 初始化MySQL连接
void http_request::init_mysql(SQLConnectionPool *conn_pool)
{
    // 保存连接池指针以便后续请求按需获取连接
    sql_conn_pool_ = conn_pool;

    SQLConnectionRAII mysql_raii(&mysql_, conn_pool); // 创建RAII连接对象，自动管理资源获取和释放

    // 在已连接的数据库中查找user表中的username,password数据
    // 如果没有user表，则创建
    if (!mysql_raii.GetConnectionPool()->FindTableExists(mysql_, "user"))
    {
        std::unordered_map<std::string, std::string> columns; // 数据表的列名和列类型
        columns["username"] = "CHAR(50)";
        columns["password"] = "CHAR(50)";
        mysql_raii.GetConnectionPool()->CreateTable(mysql_, "user", columns);
    }
    if (mysql_query(mysql_, "SELECT username, password FROM user"))
    {
        std::cout << "MYSQL获取用户名与密码错误: " << mysql_error(mysql_) << std::endl;
        return;
    }

    // 获取完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql_);
    if (!result)
        return;

    // 从结果集中获取每一行的数据，将用户名和密码预先存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string username = row[0] ? row[0] : "";
        std::string password = row[1] ? row[1] : "";
        user_data_[username] = password;
    }
    mysql_free_result(result);
}

// 关闭连接
void http_request::close_connection(bool real_close)
{
    if (real_close && sockfd_ != -1)
    {
        std::cout << "close connection: " << sockfd_ << std::endl;
        close(sockfd_);
        sockfd_ = -1;
        user_count_--;
    }
}

// 解析http请求
http_message::HTTP_CODE http_request::process_request()
{
    LINE_STATUS cur_line_status = LINE_OK; // 当前行的读取状态
    char *text = 0;                        // 当前解析行的起始位置
    http_message::HTTP_CODE http_status;   // http请求状态
    while ((check_state_ == CHECK_STATE_CONTENT && cur_line_status == LINE_OK) || (cur_line_status = parse_line()) == LINE_OK)
    { // 当前行为完整行
        text = read_buf_ + start_line_;
        start_line_ = checked_idx_;

        switch (check_state_)
        {
        case CHECK_STATE_REQUESTLINE: // 解析请求行
        {
            http_status = parse_request_line(text);
            if (http_status == BAD_REQUEST)
            {
                std::cout << "parse request line have not complete..." << std::endl; // TODO: 写进日志
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER: // 解析请求头
        {
            http_status = parse_request_headers(text);
            if (http_status == BAD_REQUEST)
            {
                std::cout << "parse request header have some error..." << std::endl; // TODO: 写进日志
                return BAD_REQUEST;
            }
            else if (http_status == GET_REQUEST)
            { // 获得完整请求，则处理请求
                return do_request();
            }
            else
                break;
        }
        case CHECK_STATE_CONTENT: // 解析请求体
        {
            http_status = parse_request_content(text);
            if (http_status == BAD_REQUEST)
            {
                std::cout << "parse request content have some error..." << std::endl; // TODO: 写进日志
                return BAD_REQUEST;
            }
            else if (http_status == GET_REQUEST)
            { // 获得完整请求，则处理请求
                return do_request();
            }
            else if (http_status == NO_REQUEST)
            { // 请求不完整
                cur_line_status = LINE_OPEN;
            }
            else
                break;
        }
        default:
        {
            std::cout << "server internal have some error..." << std::endl; // TODO: 写进日志
            return INTERNAL_ERROR;
        }
        }
    }
    return NO_REQUEST;
}

// 提取请求报文的一行
http_message::LINE_STATUS http_request::parse_line()
{
    char cur; // 当前正在分析的字符
    for (; checked_idx_ < read_idx_; checked_idx_++)
    {
        cur = read_buf_[checked_idx_];
        if (cur == '\r')
        {
            if (read_buf_[checked_idx_ + 1] == '\n')
            { // 当前字符和下一个字符为‘\r\n’时，说明是一行, 将这两个字符修改为'\0'
                read_buf_[checked_idx_++] = '\0';
                read_buf_[checked_idx_++] = '\0';
                return LINE_OK;
            }
            else if ((checked_idx_ + 1) == read_idx_)
            { // 当前字符的下一个到达末尾，行数据不完整
                return LINE_OPEN;
            }
            else
                return LINE_BAD; // 否则行出错
        }
        else if (cur == '\n')
        {
            if (checked_idx_ > 1 && read_buf_[checked_idx_ - 1] == '\r')
            { // 两个字符为“\r\n”
                read_buf_[checked_idx_ - 1] = '\0';
                read_buf_[checked_idx_++] = '\0';
                return LINE_OK;
            }
            else
                return LINE_BAD; // 只出现一个单独的换行符时，行出错
        }
    }
    return LINE_OPEN; // 循环内未返回LINE_OK，表示行数据不完整
}

// 解析http请求行,获取请求方法、请求资源路径url、HTTP版本号
http_message::HTTP_CODE http_request::parse_request_line(char *text)
{
    url_ = strpbrk(text, " \t"); // 查找第一个空格或制表符
    if (url_ == NULL)
    {
        return BAD_REQUEST;
    }

    *url_++ = '\0'; // 将空格或制表符置为\0,并将url_指向下一个字符，此时text指向请求方法（text = "GET"）
    // 解析请求方法
    static const http_message::METHOD methods[] = {
        GET, HEAD, POST, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    static const char *method_strings[] = {
        "GET", "HEAD", "POST", "PUT", "DELETE", "TRACE", "OPTIONS", "CONNECT", "PATCH"};
    bool found = false;
    for (int i = 0; i <= http_message::PATCH; i++)
    {
        if (strcmp(text, method_strings[i]) == 0)
        {
            method_ = methods[i];
            found = true;
            break;
        }
    }
    if (!found)
    {
        perror("unknown method");
        return BAD_REQUEST;
    }
    // std::cout << "method_: \n" << method_strings[method_] << "\n"<< std::endl;
    // 解析HTTP版本号
    url_ += strspn(url_, " \t");     // 跳过空格或制表符
    version_ = strpbrk(url_, " \t"); // 查找第一个空格或制表符

    if (version_ == NULL)
    {
        return BAD_REQUEST;
    }
    *version_++ = '\0';                  // 将空格或制表符置为\0,并将version_指向下一个字符, url_指向请求资源路径(/xxx)
    version_ += strspn(version_, " \t"); // 跳过空格或制表符

    // std::cout << "url_: \n" << url_ << "\n"<< std::endl;
    // std::cout << "version_: \n" << version_ << "\n"<< std::endl;

    if (strcasecmp(version_, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // 处理绝对url(例如http://localhost:8888/about)，提取出资源路径部分(/about)
    if (strncasecmp(url_, "http://", 7) == 0)
    {
        url_ += 7;
        url_ = strchr(url_, '/'); // 查找第一个'/'
    }
    if (strncasecmp(url_, "https://", 8) == 0)
    {
        url_ += 8;
        url_ = strchr(url_, '/');
    }
    if (url_ == NULL || url_[0] != '/')
    {
        perror("invalid url");
        return BAD_REQUEST;
    }
    // 当url为/时，显示索引界面
    //  if (strlen(url_) == 1)
    //      strcat(url_, "index.html");

    check_state_ = CHECK_STATE_HEADER; // 主状态机检查状态变成检查请求头

    return NO_REQUEST;
}
// 解析请求头
http_message::HTTP_CODE http_request::parse_request_headers(char *text)
{

    if (text[0] == '\0')
    { // 遇到空行，表示头部字段解析完毕
        if (content_length_ != 0)
        { // 若请求有消息体，则状态机转移到CHECK_STATE_CONTENT状态
            check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST; // 否则说明我们已经得到了一个完整的HTTP请求
    }

    int len = strchr(text, ':') - text; // 查找第一个冒号
    std::string headers_mes = std::string(text, len);
    std::string headers_value = std::string(text + len + 2); // 跳过冒号和空格

    headers_[headers_mes] = headers_value; // 存入map
    // 设置主要的请求头部信息
    if (headers_mes == "Host")
    {
        host_ = (char *)headers_value.c_str();
    }
    else if (headers_mes == "Connection")
    {
        if (strcasecmp(headers_value.c_str(), "keep-alive") == 0)
        {
            // TODO: 需要设置给reply中的keep_alive_标志
            keep_alive_ = true;
        }
    }
    else if (headers_mes == "Content-Length")
    {
        content_length_ = atoi(headers_value.c_str());
    }
    else
    {
        // TODO: 其他头部信息,暂不处理,写进日志
    }

    return NO_REQUEST;
}
// 解析请求体
http_message::HTTP_CODE http_request::parse_request_content(char *text)
{
    if (read_idx_ >= (content_length_ + checked_idx_))
    {
        text[content_length_] = '\0';
        content_ = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 处理请求，生成响应
http_message::HTTP_CODE http_request::do_request()
{
    std::cout << "-----------处理请求do_request()---------------" << std::endl;
    // 生成客户请求的目标文件的完整路径
    strcpy(html_file_, html_root_);
    int len = strlen(html_root_);
    // std::cout << "html_root_: " << html_root_ << std::endl;

    // std::cout << "url_: " << url_ << std::endl;
    const char *request_url_ = strrchr(url_, '/');
    std::cout << "request_url_: " << request_url_ << std::endl;

    // 生成目标文件路径
    char target_html_file[FILENAME_LEN];
    memset(target_html_file, '\0', sizeof(target_html_file));
    // 请求根目录时，映射到index.html
    if (strcmp(request_url_, "/") == 0 || strcmp(request_url_, "/index") == 0 || strcmp(request_url_, "/index.html") == 0)
    {
        std::cout << "请求根目录,导航到index.html" << std::endl;
        snprintf(target_html_file, FILENAME_LEN, "%s/index.html", html_root_);
    }
    else
    { // 根据html中的action产生请求其他页面
        // 检查最后一段是否有扩展名
        const char *last_slash = strrchr(url_, '/');
        const char *last_dot = strrchr(url_, '.');
        bool has_extension = (last_dot != NULL && (!last_slash || last_dot > last_slash));
        if (has_extension)
        {
            // 有扩展名，拼接html_root_ + url_
            snprintf(target_html_file, FILENAME_LEN, "%s%s", html_root_, url_);
        }
        else
        {
            // 没有扩展名，拼接html_root_ + url_ + .html
            snprintf(target_html_file, FILENAME_LEN, "%s%s.html", html_root_, url_);
        }
    }
    // 将target_html_file的内容复制到html_file_
    strncpy(html_file_, target_html_file, FILENAME_LEN - 1);
    std::cout << "html文件路径: " << html_file_ << std::endl;

    // 仅当请求为 POST 且有 body 时，解析 form-urlencoded 的 body 到键值 map
    std::string form_name, form_password;
    if (method_ == POST && content_ != NULL && content_length_ > 0)
    {
        std::string body(content_, content_length_);
        std::cout << "content_: " << body << std::endl;
        // 简单解析 key=value&key2=value2 形式（不处理 URL 解码）
        size_t pos = 0;
        while (pos < body.size())
        {
            size_t amp = body.find('&', pos);
            std::string kv = body.substr(pos, (amp == std::string::npos) ? std::string::npos : amp - pos);
            size_t eq = kv.find('=');
            if (eq != std::string::npos)
            {
                std::string k = kv.substr(0, eq);
                std::string v = kv.substr(eq + 1);
                if (k == "user" || k == "username")
                    form_name = v;
                else if (k == "password")
                    form_password = v;
            }
            if (amp == std::string::npos)
                break;
            pos = amp + 1;
        }
    }

    // 处理注册完成请求：仅在 /register_done 且为 POST 时执行写数据库逻辑
    if (strcmp(url_, "/register_done") == 0)
    {
        std::cout << "注册用户: " << form_name << ", 密码: " << form_password << std::endl;
        if (method_ != POST)
        {
            // 非 POST 请求，仅返回 register_done 页面（通常不会发生），保留映射的 html_file_
        }
        else if (form_name.empty() || form_password.empty())
        {
            // POST 但表单不完整，跳转到错误页
            std::cout << "注册表单不完整" << std::endl;
            std::string new_url_ = "registerError.html";
            snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
        }
        else
        {
            // 判断用户是否已存在
            if (user_data_.find(form_name) != user_data_.end())
            {
                std::cout << "用户已存在: " << form_name << std::endl;
                std::string new_url_ = "registerError.html";
                snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
            }
            else
            {
                // 将用户信息写入数据库
                std::string sql_insert = "INSERT INTO user (username, password) VALUES ('" + form_name + "', '" + form_password + "')";
                locker_.lock();
                int res = -1;
                // 使用 RAII 按需获取连接，避免依赖对象成员 mysql_
                if (sql_conn_pool_)
                {
                    SQLConnectionRAII conn_raii(&mysql_, sql_conn_pool_);
                    if (mysql_)
                    {
                        res = mysql_query(mysql_, sql_insert.c_str());
                    }
                    else
                    {
                        std::cout << "无法从连接池获取 MySQL 连接" << std::endl;
                    }
                }
                else
                {
                    std::cout << "sql_conn_pool_ 未初始化，无法插入用户信息" << std::endl;
                }

                if (res == 0)
                {
                    user_data_[form_name] = form_password;
                    std::cout << "用户注册成功: " << form_name << std::endl;
                    std::string new_url_ = "login.html";
                    snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
                }
                else
                {
                    if (mysql_ != NULL)
                        std::cout << "MYSQL插入用户信息错误: " << mysql_error(mysql_) << std::endl;
                    std::string new_url_ = "registerError.html";
                    snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
                }
                locker_.unlock();
            }
        }
    }

    // 处理登录请求：仅在 /login_done 且为 POST 时执行验证逻辑
    if (strcmp(url_, "/login_done") == 0)
    {
        std::cout << "登录用户名: " << form_name << ", 密码: " << form_password << std::endl;
        if (method_ != POST)
        {
            // 非 POST 请求，仅返回 login_done 页面（通常不会发生）
        }
        else if (form_name.empty() || form_password.empty())
        {
            std::cout << "登录表单不完整" << std::endl;
            std::string new_url_ = "loginError.html";
            snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
        }
        else
        {
            auto it = user_data_.find(form_name);
            if (it != user_data_.end() && it->second == form_password)
            {
                std::cout << "用户登录成功: " << form_name << std::endl;
                std::string new_url_ = "function_choice.html";
                snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
            }
            else
            {
                std::cout << "用户登录失败: " << form_name << std::endl;
                std::string new_url_ = "loginError.html";
                snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
            }
        }
    }

    // 图片页面
    if (strcmp(url_, "/pic") == 0)
    {
        std::cout << "处理图片请求" << std::endl;
        std::string new_url_ = "picture.html";
        snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
    }
    // 视频播放页面
    if (strcmp(url_, "/video") == 0)
    {
        std::cout << "处理视频请求" << std::endl;
        std::string new_url_ = "video.html";
        snprintf(html_file_, FILENAME_LEN, "%s/%s", html_root_, new_url_.c_str());
    }

    // 获取文件属性，状态存放在file_stat_中
    int ret = stat(html_file_, &file_stat_);
    if (ret < 0)
    {
        std::cout << "html文件未找到: " << html_file_ << std::endl; // 返回404界面？
        return NO_RESOURCE;                                         // 目标文件不存在
    }
    if ((file_stat_.st_mode & S_IROTH) == 0)
    {                                                               // 判断文件是否有读权限
        std::cout << "html文件无权限: " << html_file_ << std::endl; // 返回403界面？
        return FORBIDDEN_REQUEST;                                   // 无访问权限
    }
    if (S_ISDIR(file_stat_.st_mode))
    {
        return BAD_REQUEST; // 目标文件是目录
    }

    int fd = open(html_file_, O_RDONLY); // 以只读方式打开文件
    if (fd < 0)
    {
        std::cout << "打开文件失败: " << html_file_ << " errno:" << errno << std::endl;
        return NO_RESOURCE;
    }
    // 文件内存映射
    html_file_addr_ = (char *)mmap(NULL, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (html_file_addr_ == MAP_FAILED)
    {
        std::cout << "mmap 文件失败: " << html_file_ << " size:" << file_stat_.st_size << std::endl;
        html_file_addr_ = NULL;
        return NO_RESOURCE;
    }
    return FILE_REQUEST; // 文件请求，获取文件成功
}

void http_request::unmap()
{
    if (html_file_addr_)
    {
        munmap(html_file_addr_, file_stat_.st_size);
        html_file_addr_ = NULL;
    }
}

// 从读缓冲区读数据
bool http_request::read_buffer()
{
    if (read_idx_ >= READ_BUFFER_SIZE)
    {
        return false;
    }

    // 非阻塞 socket：
    // - LT: 读一次即可（若 EAGAIN 说明本次没数据，不算错误）
    // - ET: 必须循环读到 EAGAIN/EWOULDBLOCK 才算把本次事件“吃干净”
    bool got_any = false;
    while (true)
    {
        ssize_t n = recv(sockfd_, read_buf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
        if (n > 0)
        {
            read_idx_ += n;
            got_any = true;
            // 缓冲区满了就返回，交给上层做 BAD_REQUEST/400
            if (read_idx_ >= READ_BUFFER_SIZE)
            {
                return true;
            }
            // LT 模式下读一次就够了
            if (trig_mode_ == 0)
            {
                return true;
            }
            // ET 继续循环
            continue;
        }
        if (n == 0)
        {
            // 对端关闭连接
            return false;
        }
        // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 已经读完本次可读数据：
            // - 如果之前读到过数据：成功
            // - 如果一次都没读到：本次事件可能是边缘触发误唤醒/竞态，不应关闭连接
            return got_any;
        }
        if (errno == EINTR)
        {
            continue;
        }
        return false;
    }
}
