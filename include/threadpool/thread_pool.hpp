#ifndef THREADPOOL_HPP_
#define THREADPOOL_HPP_

#include <list>
#include <pthread.h>
#include <cstdio>
#include <exception>
#include <unordered_map>

#include "lock/locker.hpp"
#include "CGImysql/sql_connection_pool.hpp"
#include "threadpool/handle_set.hpp"
#include "threadpool/thread_set.hpp"
#include "threadpool/event_handler.hpp"

template <typename T>
class ThreadPool {
public:
    /**
     * @brief 构造函数
     * @param actor_mode: 线程池的事件模式，0表示Proactor模式，1表示Reactor模式
     * @param concurrent_mode: 线程池的并发模型，0表示半同步/半异步模型，1表示领导者/跟随者模型
     * @param sql_conn_pool: 数据库连接池
     * @param thread_num: 线程池中线程的数量
     * @param max_requests: 线程池中允许的最大请求数量
     */
    ThreadPool(int actor_mode, int concurrent_mode, SQLConnectionPool *sql_conn_pool, HandleSet *handle_set, int thread_num = 8, int max_requests = 10000);

    ~ThreadPool();

    /**
     * @brief 添加任务到线程池
     * @param request: 请求对象
     * @param state: 请求状态
     * @return true: 添加成功; false: 添加失败
     */
    bool appendWithState(T *request, int state);
    /**
     * @brief 添加任务到线程池,不带状态
     * @param request: 请求对象
     * @return true: 添加成功; false: 添加失败
     */
    bool append(T *request);


    /**
     * @brief 注册连接到句柄集
     * @param connfd: 连接的文件描述符
     * @return true: 注册成功; false: 注册失败
     */
    bool registerConnection(int connfd, EventHandler* handler, uint32_t events);


private:
    // 工作线程运行函数
    static void* worker(void* arg);
    void run_HS_HA();           // 以半同步/半异步模型运行
    void run_L_F();             // 以领导者/跟随者模型运行

    int thread_num_;            // 线程池中的线程数
    pthread_t* threads_;        // 描述线程池的数组，其大小为thread_num_
    int max_requests_;          // 请求队列中允许的最大请求数
    std::list<T*> task_queue_;  // 任务队列（存储指针）
    MutexLock queue_locker_;    // 保护任务队列的互斥锁
    Semaphore queue_sem_;       // 任务队列的信号量
    SQLConnectionPool *sql_conn_pool_; // 数据库连接池

    int actor_mode_;             // 线程池的事件模式(0: Proactor, 1: Reactor)
    int concurrent_mode_;        // 线程池的并发模型(0: 半同步/半异步, 1: 领导者/跟随者)

    // L/F模式内容
    HandleSet* handle_set_;    // 事件处理器集合
    ThreadSet thread_set_;     // 线程集合
    std::unordered_map<int, EventHandler*> lf_handlers_;
};


#endif // THREADPOOL_HPP_