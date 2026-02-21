#include "threadpool/thread_pool.hpp"
#include "http/http_message.hpp"
#include <iostream>
#include <atomic>
#include <mutex>

template <typename T>
ThreadPool<T>::ThreadPool(int actor_mode, int concurrent_mode, SQLConnectionPool *sql_conn_pool, HandleSet *handle_set, int thread_num, int max_requests)
    : actor_mode_(actor_mode), concurrent_mode_(concurrent_mode), thread_num_(thread_num), sql_conn_pool_(sql_conn_pool), max_requests_(max_requests), threads_(NULL), handle_set_(handle_set)
{
    if (thread_num <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }
    threads_ = new pthread_t[thread_num_];
    if (!threads_)
    {
        throw std::exception();
    }
    // 预先创建thread_num_个数的线程
    for (int i = 0; i < thread_num_; ++i)
    {
        int ret = pthread_create(&threads_[i], NULL, worker, this);
        if (ret)
        {
            delete[] threads_;
            throw std::exception();
        }
        // 线程分离，避免僵尸进程
        ret = pthread_detach(threads_[i]);
        if (ret)
        {
            delete[] threads_;
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
    if (handle_set_ != nullptr)
    {
        for (auto &kv : lf_handlers_)
        {
            handle_set_->unregister_handle(kv.first);
            delete kv.second;
        }
        lf_handlers_.clear();
    }
    delete[] threads_;
}

// 添加任务,带有读\写状态
template <typename T>
bool ThreadPool<T>::appendWithState(T *task, int state)
{
    if (task == nullptr)
    {
        LOG_ERROR("%s", "[ThreadPool] 任务指针为空");
        return false;
    }
    queue_locker_.lock();
    if (task_queue_.size() >= max_requests_)
    {
        LOG_ERROR("%s", "[ThreadPool] Task队列已满");
        queue_locker_.unlock();
        return false;
    }
    task->io_state_ = state;     // 设置 I/O 状态：0=读，1=写
    task_queue_.push_back(task); // 网络连接入队任务队列
    queue_locker_.unlock();
    queue_sem_.post();
    return true;
}

// 添加任务，不带读\写状态
template <typename T>
bool ThreadPool<T>::append(T *task)
{
    if (task == nullptr)
    {
        LOG_ERROR("%s", "[ThreadPool] 任务指针为空");
        return false;
    }
    queue_locker_.lock();
    if (task_queue_.size() >= max_requests_)
    {
        LOG_ERROR("%s", "[ThreadPool] Task队列已满");
        queue_locker_.unlock();
        return false;
    }
    task_queue_.push_back(task); // 网络连接入队任务队列
    queue_locker_.unlock();
    queue_sem_.post();
    return true;
}

// LF模式，注册连接及对应的事件处理器到句柄集
template <typename T>
bool ThreadPool<T>::registerConnection(int connfd, EventHandler *handler, uint32_t events)
{
    if (handle_set_ == nullptr || handler == nullptr || connfd < 0)
    {
        return false;
    }

    handle_set_->register_handle(connfd, handler, events);
    return true;
}

// 线程的工作函数
template <typename T>
void *ThreadPool<T>::worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    if (pool->concurrent_mode_ == 0)
    {
        // 半同步/半异步模型
        pool->run_HS_HA();
    }
    else
    {
        // 领导者/跟随者模型
        pool->run_L_F();
    }

    return pool;
}

// 线程的运行函数（以半同步/半异步模型运行）
template <typename T>
void ThreadPool<T>::run_HS_HA()
{
    static std::atomic<int> hs_ha_counter{0};
    int hs_id = ++hs_ha_counter;
    static std::mutex log_mutex;
    {
        std::lock_guard<std::mutex> lg(log_mutex);
        LOG_INFO("%s", ("[ThreadPool][HS/HA][#" + std::to_string(hs_id) + "][tid=" + std::to_string(pthread_self()) + "] 启动").c_str());
    }
    while (true)
    {
        queue_sem_.wait();    // P操作：等待信号量，等待可用任务线程
        queue_locker_.lock(); // 保护任务队列
        if (task_queue_.empty())
        {
            queue_locker_.unlock();
            continue;
        }
        // 获取任务
        T *task = task_queue_.front(); // task类型为http_reply(继承了http_request)
        task_queue_.pop_front();
        queue_locker_.unlock();
        if (task == nullptr)
        {
            continue;
        }
        if (actor_mode_ == 0)
        { // Reactor模式：工作线程负责 I/O 和业务逻辑
            // 处理请求
            if (0 == task->io_state_)
            { // 读状态
                LOG_INFO("%s", ("[ThreadPool] Reactor 读取数据, fd=" + std::to_string(task->get_sockfd())).c_str());
                bool ret = task->read_buffer();
                if (ret)
                {
                    LOG_INFO("%s", "[ThreadPool] 读取成功, 开始处理请求");
                    task->improved_state_ = 1; // 标记为已处理
                    // 按需获取数据库连接（RAII 作用域内有效）
                    SQLConnectionRAII mysqlconn(&task->mysql_, sql_conn_pool_);

                    // 1. 解析 HTTP 请求，得到处理结果码
                    http_message::HTTP_CODE read_ret = task->process_request();
                    LOG_INFO("%s", ("[ThreadPool] 请求解析结果: " + std::to_string(read_ret)).c_str());

                    // 2. 初始化响应相关参数
                    task->init_reply();

                    // 3. 根据请求处理结果生成响应内容
                    bool write_ok = task->process_reply(read_ret);
                    if (!write_ok)
                    {
                        LOG_ERROR("%s", "[ThreadPool] 生成响应失败");
                        task->timer_flag_ = 1;
                        task->close_connection(true);
                        continue;
                    }

                    // 4. 发送响应到客户端
                    LOG_INFO("%s", ("[ThreadPool] 发送响应, fd=" + std::to_string(task->get_sockfd())).c_str());
                    if (!task->write(task->get_sockfd()))
                    {
                        LOG_ERROR("%s", "[ThreadPool] 发送响应失败");
                        task->timer_flag_ = 1;
                        task->close_connection(true);
                    }
                    else
                    {
                        LOG_INFO("%s", "[ThreadPool] 发送响应成功");
                        // 非长连接则关闭
                        if (!task->is_keep_alive())
                        {
                            task->close_connection(true);
                        }
                    }
                }
                else
                {
                    LOG_ERROR("%s", ("[ThreadPool] 读取数据失败, fd=" + std::to_string(task->get_sockfd())).c_str());
                    task->improved_state_ = 1;
                    task->timer_flag_ = 1;
                    task->close_connection(true);
                }
            }
            else
            { // 写状态
                if (task->write(task->get_sockfd()))
                {
                    task->improved_state_ = 1;
                    if (!task->is_keep_alive())
                    {
                        task->close_connection(true);
                    }
                }
                else
                {
                    task->improved_state_ = 1;
                    task->timer_flag_ = 1;
                    task->close_connection(true);
                }
            }
        }
        else
        { // Proactor模式（主线程已完成 I/O，工作线程只处理业务逻辑）
            // 按需获取数据库连接
            SQLConnectionRAII mysqlconn(&task->mysql_, sql_conn_pool_);

            // 1. 解析 HTTP 请求
            http_message::HTTP_CODE read_ret = task->process_request();

            // 2. 初始化响应参数
            task->init_reply();

            // 3. 生成响应
            bool write_ok = task->process_reply(read_ret);
            if (!write_ok)
            {
                task->close_connection(true);
                continue;
            }

            // 4. 发送响应
            if (!task->write(task->get_sockfd()))
            {
                task->close_connection(true);
            }
        }
    }
}

// 线程运行函数（以领导者/跟随者模型运行）
template <typename T>
void ThreadPool<T>::run_L_F()
{
    static std::atomic<int> lf_counter{0};
    int lf_id = ++lf_counter;
    static std::mutex log_mutex;
    {
        std::lock_guard<std::mutex> lg(log_mutex);
        LOG_INFO("%s", ("[ThreadPool][L/F][#" + std::to_string(lf_id) + "][tid=" + std::to_string(pthread_self()) + "] 启动").c_str());
    }
    if (handle_set_ == nullptr)
    {
        LOG_ERROR("%s", "[ThreadPool] L/F: handle_set_ is nullptr");
        return;
    }
    while (true)
    {
        // 当前线程加入线程集，等待成为领导者
        thread_set_.join();
        // 现在是领导者，负责监听I/O事件
        LOG_INFO("%s", ("[L/F] 线程 " + std::to_string(pthread_self()) + " 成为领导者").c_str());

        // 等待IO事件
        int ready_count = handle_set_->wait_for_event(-1); // 阻塞等待I/O事件
        if (ready_count <= 0)
            continue; // 没有准备好的IO事件，继续阻塞等待

        // 有准备好的IO事件，当前线程处理事件，需要推选一个新的领导者
        thread_set_.promote_new_leader();

        LOG_INFO("%s", ("[L/F] 线程 " + std::to_string(pthread_self()) + " 推选新的领导者").c_str());

        // 处理准备好的IO事件
        for (int i = 0; i < ready_count; ++i)
        {
            auto [handler, events] = handle_set_->get_ready_handler(i);
            if (handler)
            {
                handler->handle_event(events);
            }
        }

        // 状态转换
        thread_set_.transition_state();

        LOG_INFO("%s", ("[L/F] 线程 " + std::to_string(pthread_self()) + " 处理完成").c_str());
    }
}

// 显式模板实例化
#include "http/http_reply.hpp"
template class ThreadPool<http_reply>;