/*
阻塞队列，封装生产者-消费者缓冲区
在多线程程序中把生产者产生的数据（如要写的日志、待处理任务等）缓冲到队列里，由消费者线程阻塞等待并取出处理，从而解耦生产与消费速率。
*/

#ifndef BLOCK_QUEUE_HPP
#define BLOCK_QUEUE_HPP

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <queue>
#include <algorithm>

#include "lock/locker.hpp"

template<class T>
class block_queue
{
public:
    block_queue(int max_size = 1000){
        if(max_size <= 0){
            perror("queue max size less zero");
            exit(-1);
        }
        max_size_ = max_size;
    }

    ~block_queue(){
        mutex_.lock();
        if(queue_)  delete queue_;
        mutex_unlock(); 
    }

    // 清空队列
    void clear(){
        mutex_.lock();
        std::queue<T> empty;
        swap(empty, queue_);
        mutex.unlock();
    }

    // 判断队列是否满
    bool isFull(){
        mutex_.lock();
        if(queue_.size() > max_size_)  {
            mutex_.unlock();
            return true;
        }
        mutex_.unlock();
        return false;
    }

    // 判断队列是否为空
    bool isEmpty(){
        mutex_.lock();
        if(queue_.empty()){
            mutex_.unlock();
            return true;
        }
        mutex_.unlock();
        return false;
    }

    // 返回队首元素
    bool front(T &value){
        mutex_.lock();
        if(size_ == 0){
            mutex_.unlock();
            return false;
        }
        value = queue_.front();
        mutex_.unlock();
        return true;
    }

    // 返回队尾元素
    bool back(T &value){
        mutex_.lock();
        if(size_ == 0){
            mutex_.unlock();
            return false;
        }
        value = queue_.back();
        mutex_.unlock();
        return true;
    }

    // 获取队列容量
    int getSize(){
        mutex_.lock();
        int size = queue_.size();
        mutex_.unlock();
        return size;
    }

    // 获取队列最大容量
    int getMaxSize(){
        mutex_.lock();
        int max = max_size_;
        mutex_.unlock();
        return max;
    }

    // 向队列添加元素，生产者生产元素，将所有使用队列的线程唤醒
    bool push(const T &item){
        mutex_.lock();
        if(queue_.size() >= max_size_){
            cond_.broadcast();      // 唤醒使用队列的进程，但是此时没有元素入队，消费者无法使用
            mutex_.unlock();
            return false;
        }
        queue_.emplace(item);
        cond_.broadcast();  // 唤醒使用队列的线程
        mutex_.unlock();
        return true;
    }

    // 阻塞，直到有元素可取
    bool pop(T &item){
        mutex_.lock();
        // 多个消费者竞争时，一个消费者拿到了生产的，其他消费者还需要等待
        while(queue_.size() <= 0){
            // 如果while或者if判断的时候，满足执行条件，线程便会调用pthread_cond_wait阻塞自己，
            // 此时它还在持有锁，如果他不解锁，那么其他线程将会无法访问公有资源。
            if(!cond_.wait(mutex_.get())){  
                mutex_.unlock();
                return false;
            }
        }
        item = queue_.pop();
        mutex_.unlock();
        return true;
    }


private:
    MutexLock mutex_;   
    ConditionVariable cond_;

    std::queue<T> queue_;   // 阻塞队列缓冲区
    int max_size_;    // 队列的最大容量


};

#endif;