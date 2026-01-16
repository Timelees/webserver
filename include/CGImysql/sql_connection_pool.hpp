#ifndef _SQL_CONNECTION_POOL_HPP_
#define _SQL_CONNECTION_POOL_HPP_

#include <stdio.h>
#include <iostream>
#include <mysql/mysql.h>
#include <string>
#include <list>
#include <unordered_map>
#include "../lock/locker.hpp"

// 数据库连接池类
class SQLConnectionPool{
public:
    MYSQL *GetSQLConnection();     // 获取数据库连接
    bool ReleaseSQLConnection(MYSQL *conn); // 释放数据库连接
    void DestroySQLConnectionPool();    // 销毁数据库连接池
    int GetFreeConnectionCount();   // 获取当前空闲的连接数
    bool FindTableExists(MYSQL *conn, std::string table_name);      // 在已连接的数据库conn中查找table_name数据表是否存在
    void CreateTable(MYSQL *conn, std::string table_name, std::unordered_map<std::string, std::string> columns);      // 创建数据表

    // 单例模式：创建一个静态成员变量，该变量会自动初始化，且只会初始化一次，自动进行内存管理和线程安全
    static SQLConnectionPool* GetInstance(){
        static SQLConnectionPool instance;
        return &instance;
    }

    // 初始化数据库连接池
    void init(std::string url, std::string User, std::string PassWord, std::string DataBaseName, int Port, int MaxConn, bool close_log);

private:
    SQLConnectionPool();
    ~SQLConnectionPool();

    int max_conn_;          // 最大连接数
    int cur_conn_;          // 当前已使用的连接数
    int free_conn_;         // 当前空闲的连接数

    std::list<MYSQL *> conn_list_;  // 连接池
    MutexLock mutex_;
    Semaphore sem_reserve_;

public:
    std::string url_;          // 主机地址
    std::string port_;         // 数据库端口号
    std::string user_;         // 登录数据库用户名
    std::string password_;     // 登录数据库密码
    std::string database_name_;// 数据库名称
    bool close_log_ = true;	//日志开关

};

// 基于RAII的数据库连接管理类
// RAII:将资源的生命周期与对象的生命周期绑定，确保资源的自动获取和安全释放。资源的获取与对象的初始化绑定，资源的释放与对象的析构绑定
class SQLConnectionRAII{
public:
    SQLConnectionRAII(MYSQL **SQLConn, SQLConnectionPool *conn_pool);
    ~SQLConnectionRAII();
    MYSQL *GetConnection(){return SQLConnection_;};
    SQLConnectionPool *GetConnectionPool(){return conn_pool_;};


private:
    MYSQL *SQLConnection_;
    SQLConnectionPool *conn_pool_;
};

#endif  //_SQL_CONNECTION_POOL_HPP_