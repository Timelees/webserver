// L/F模式线程集组件
#ifndef THREAD_SET_HPP
#define THREAD_SET_HPP

#include <pthread.h>
#include <set>
#include "lock/locker.hpp"


enum class ThreadState{
    LEADER,
    PROCESSING,
    FOLLOWER
};

class ThreadSet{
public:
    ThreadSet();
    ~ThreadSet();
    
    /**
     * @brief 线程加入线程集， 追随者等待成为领导者
     * @return 线程是否成为领导者
     * */
    bool join();

    /**
     * @brief 领导者线程检测到IO事件后，进行相应处理
     */
    void process_by_leader();

    /**
     * @brief 指定其他追随者处理事件
     */
    void process_by_follower(pthread_t tid);

    /**
     * @brief process_by_leader()后，领导者处于process状态，此时无领导者，设置新的领导者线程
     */
    void promote_new_leader();

    /**
     * @brief 事件处理完后的线程状态转换
     * Processing状态线程处理完事件后调用：
     * 1. 如果当前没有领导者，成为新领导者
     * 2. 否则转为追随者状态
     */
    void transition_state();

    /**
     * @brief 获取当前线程状态
     */
    ThreadState get_state() const { return state_; };

    /**
     * @brief 设置当前线程状态
     */
    void set_state(ThreadState state);

    /**
     * @brief 检查是否有领导者，返回当前领导者线程ID
     */
    pthread_t has_leader();

private:
    ThreadState state_;     // 当前线程状态
    MutexLock mutex_;       // 线程同步锁
    ConditionVariable leader_cond_;     // 领导者条件变量

    bool has_leader_;   // 是否有领导者
    pthread_t leader_tid_;   // 当前领导者线程ID

    std::set<pthread_t> followers_;   // 追随者线程ID集合    
};

#endif // THREAD_SET_HPP