#ifndef LOCKER_HPP
#define LOCKER_HPP

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 信号量——控制同时访问资源的线程数量
class Semaphore{
public:
    // 使用sem_init初始化信号量，pshared=0表示用于线程间的同步控制
    Semaphore(){
        if(sem_init(&sem_, 0, 0) != 0){     // 默认构造，信号量初始值为0
            throw std::exception();
        }
    }
    Semaphore(int num){
        if(sem_init(&sem_, 0, num) != 0){   // 带参数构造，信号量初始值为num
            throw std::exception();
        }
    }
    ~Semaphore(){
        sem_destroy(&sem_);
    }
    // 以原子操作的方式给信号量的值减1（P操作），如果信号量的值为0函数将会等待，直到有线程增加了该信号量的值使其不再为0
    bool wait(){
        return sem_wait(&sem_) == 0;
    }
    // 以原子操作的方式给信号量的值加1（V操作），并发出信号唤醒等待线程 sem_wait
    bool post(){
        return sem_post(&sem_) == 0;    
    }

private:
    sem_t sem_;
};

// 互斥锁——同一时间只有一个线程可以访问共享的数据资源
class MutexLock{
public:
    MutexLock(){
        if(pthread_mutex_init(&mutex_, nullptr) != 0){
            throw std::exception();
        }
    }
    ~MutexLock(){
        pthread_mutex_destroy(&mutex_);
    }
    // 上锁
    bool lock(){
        return pthread_mutex_lock(&mutex_) == 0;
    }
    // 解锁
    bool unlock(){
        return pthread_mutex_unlock(&mutex_) == 0;
    }
    // 获取互斥锁
    pthread_mutex_t *get()
    {
        return &mutex_;
    }

private:
    pthread_mutex_t mutex_;
};

// 条件变量——用于线程间的同步
class ConditionVariable{
public:
    ConditionVariable(){
        if(pthread_cond_init(&cond_, nullptr) != 0){
            throw std::exception();
        }
    }
    ~ConditionVariable(){
        pthread_cond_destroy(&cond_);
    }
    // 等待条件变量
    bool wait(pthread_mutex_t &mutex){
        return pthread_cond_wait(&cond_, &mutex) == 0;
    }
    // 带超时的等待条件变量
    bool timewait(pthread_mutex_t &mutex, const struct timespec &timeout){
        return pthread_cond_timedwait(&cond_, &mutex, &timeout) == 0;
    }

    // 唤醒一个等待线程
    bool signal(){
        return pthread_cond_signal(&cond_) == 0;
    }
    // 唤醒所有等待线程
    bool broadcast(){
        return pthread_cond_broadcast(&cond_) == 0;
    }

private:
    pthread_cond_t cond_;
};

#endif // LOCKER_H