#include "CGImysql/sql_connection_pool.hpp"

SQLConnectionPool::SQLConnectionPool(){
    cur_conn_ = 0;
    free_conn_ = 0;
}

SQLConnectionPool::~SQLConnectionPool(){
    DestroySQLConnectionPool();
}

// 初始化数据库连接池
void SQLConnectionPool::init(std::string url, std::string User, std::string PassWord, std::string DataBaseName, int Port, int MaxConn, bool close_log){
    url_ = url;
    port_ = Port;
    user_ = User;
    password_ = PassWord;
    database_name_ = DataBaseName;
    max_conn_ = MaxConn;
    close_log_ = close_log;

    // 创建线程池，大小为max_conn_
    for(int i = 0; i < max_conn_; i++){
        MYSQL *conn = NULL;
        conn = mysql_init(conn);
        if(conn == NULL){
            // TODO: 写入日志
            std::cout << "MySQL Error: " << mysql_error(conn) << std::endl;
            exit(1);
        }
        conn = mysql_real_connect(conn, url_.c_str(), user_.c_str(), password_.c_str(),
                                   database_name_.c_str(), Port, NULL, 0);
        if (conn == NULL) {
            // TODO: 写入日志
            std::cout << "MySQL Error: " << mysql_error(conn) << std::endl;
            exit(1);
        }
        // 将连接放入连接池
        conn_list_.push_back(conn);
        free_conn_++;
    }
    // 设置信号量为空闲的连接数
    sem_reserve_ = Semaphore(free_conn_);
    max_conn_ = free_conn_;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新当前连接数和空闲连接数
MYSQL *SQLConnectionPool::GetSQLConnection() {
    MYSQL *conn = NULL;
    if (0 == conn_list_.size()){
        std::cout << "数据库连接池为空,获取连接失败!" << std::endl;
        return NULL;
    }

    if (free_conn_ > 0) {
        sem_reserve_.wait();    // 先减少一个信号量，如果剩余的连接数为0，则阻塞
        // 从连接池中获取一个连接时，通过互斥锁保护共享资源
        mutex_.lock();
        conn = conn_list_.front();
        conn_list_.pop_front();
        cur_conn_++;
        free_conn_--;
        mutex_.unlock();
    }
    return conn;
}

// 释放当前正在使用的数据库连接
bool SQLConnectionPool::ReleaseSQLConnection(MYSQL *conn) {
    if (conn == NULL){
        std::cout << "释放连接失败，连接为空!" << std::endl;
        return false;
    }
    if (conn) {
        mutex_.lock();
        conn_list_.push_back(conn);
        cur_conn_--;
        free_conn_++;
        mutex_.unlock();
        sem_reserve_.post();    // 释放一个信号量
    }
    return true;
}

// 销毁数据库连接池
void SQLConnectionPool::DestroySQLConnectionPool(){
    mutex_.lock();
    if(conn_list_.size() > 0){
        for(auto conn : conn_list_){
            mysql_close(conn);
        }
        cur_conn_ = 0;
        free_conn_ = 0;
        conn_list_.clear();
    }
    mutex_.unlock();
}

// 获取当前空闲的连接数
int SQLConnectionPool::GetFreeConnectionCount() {
    return free_conn_;
}

// 查找已连接的数据库中是否有指定的数据表
bool SQLConnectionPool::FindTableExists(MYSQL *conn, std::string table_name){
    // SELECT TABLE_NAME  FROM INFORMATION_SCHEMA.TABLES  WHERE TABLE_SCHEMA = DATABASE()    AND TABLE_NAME = 'user'  LIMIT 1;
    std::string query = "SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '" + table_name + "' LIMIT 1;";
    if(mysql_query(conn, query.c_str())){
        std::cout << "MYSQL查询数据表(" << table_name << ")错误: " << mysql_error(conn) << std::endl;
        return false;
    }
    MYSQL_RES *result = mysql_store_result(conn);
    if(result == NULL){
        std::cout << "MYSQL获取结果集错误: " << mysql_error(conn) << std::endl;
        return false;
    }
    bool exists = (mysql_num_rows(result) > 0);     // 存在table_name, 返回true
    mysql_free_result(result);
    return exists;
}

void SQLConnectionPool::CreateTable(MYSQL *conn, std::string table_name, std::unordered_map<std::string, std::string> columns){
    std::string create_table_query = "CREATE TABLE " + table_name + " (";
    for(auto &col: columns){
        create_table_query += col.first + " " + col.second + " NULL" +",";
    }
    create_table_query = create_table_query.substr(0, create_table_query.size()-1); // 去掉最后一个逗号
    create_table_query += ") ENGINE=InnoDB CHARSET=utf8;";
    if(mysql_query(conn, create_table_query.c_str())){
        std::cout << "MYSQL创建数据表(" << table_name << ")错误: " << mysql_error(conn) << std::endl;
    }else{
        std::cout << "MYSQL创建数据表(" << table_name << ")成功!" << std::endl;
    }
}


// RAII构造, 设置数据库连接池和从连接池中获取的连接MYSQL对象
SQLConnectionRAII::SQLConnectionRAII(MYSQL **SQLConn, SQLConnectionPool *conn_pool){
    *SQLConn = conn_pool->GetSQLConnection();

    SQLConnection_ = *SQLConn;
    conn_pool_ = conn_pool;
}

// 析构函数自动释放资源
SQLConnectionRAII::~SQLConnectionRAII(){
    if(SQLConnection_){
        conn_pool_->ReleaseSQLConnection(SQLConnection_);
    }
}