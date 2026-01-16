#include "threadpool/thread_set.hpp"

// 线程加入到线程集，若没有领导者则成为领导者
bool ThreadSet::join(){
    mutex_.lock();
    pthread_t current_tid = pthread_self(); // 当前线程的id
    // 如果没有领导者，当前线程成为领导者
    if(!has_leader_){
        leader_tid_ = current_tid;
        has_leader_ = true;
        state_ = ThreadState::LEADER;
        mutex_.unlock();
        return true
    }

    // 如果有领导者，当前线程成为追随者
    followers_.insert(current_tid);
    state_ = ThreadState::FOLLOWER;

    // 对于追随者，根据条件变量等待成为领导者
    while(state_ == ThreadState::FOLLOWER && has_leader_ && leader_tid_ != current_tid){
        int ret = leader_cond_.wait(*mutex_.get());    // 条件变量阻塞等待，直到重新获取到互斥锁，并且条件满足
        if(ret == 0){
            // 追随者线程被唤醒,若没有领导者则尝试成为领导者
            if(!has_leader_){
                leader_tid_ = current_tid;
                has_leader_ = true;
                state_ = ThreadState::LEADER;
                followers_.erase(current_tid);
                mutex_.unlock();
                return true;
            }
        }
    }
}

// 领导者线程检测到IO事件后，处理
void ThreadSet::process_by_leader(){
    // 处理IO事件
    if(state_ == ThreadState::LEADER){
        // 领导者处理IO事件
        state_ = ThreadState::PROCESSING;   // 进入处理状态

        // IO事件处理逻辑


        // 设置新的领导者
        promote_new_leader();
    }
}

// 指定其他追随者处理事件
void ThreadSet::process_by_follower(pthread_t tid){
    // 处理IO事件
    if(state_ == ThreadState::FOLLOWER){
        // 追随者处理IO事件
        if(tid == leader_tid_){
            // 如果指定的线程是领导者，进行相应处理
            process_by_leader();
        }else{
            // 如果指定的线程是追随者，进行相应处理
            state_ = ThreadState::PROCESSING;   // 进入处理状态

            // IO事件处理逻辑


        }
    }
}


// 设置新的领导者
void ThreadSet::promote_new_leader(){
    mutex_.lock();

    if(!followers_.empty() && !has_leader_){
        // 推选一个新的领导者
        has_leader_ = false;    // 当前没有领导者
        leader_cond_.signal();  // 唤醒等待的追随者
    }else{
        // 没有追随者，无领导者
        has_leader_ = false;
    }

    mutex_.unlock();
}

// 事件处理完后，状态切换
void ThreadSet::transition_state(){
    mutex_.lock();

    if(!has_leader_){
        // 当前没有领导者，当前线程尝试成为领导者
        leader_tid_ = pthread_self();
        has_leader_ = true;
        state_ = ThreadState::LEADER;
    }else{
        // 当前已经有领导者，当前线程成为追随者
        followers_.insert(pthread_self());
        state_ = ThreadState::FOLLOWER;
    }

    mutex_.unlock();
}

// 设置当前线程状态
void ThreadSet::set_state(ThreadState state){
    mutex_.lock();
    state_ = state;
    mutex_.unlock();
}

// 检查是否有领导者，返回当前领导者线程ID
pthread_t ThreadSet::has_leader(){
    mutex_.lock();
    pthread_t leader_tid = has_leader_ ? leader_tid_ : pthread_t();
    mutex_.unlock();
    return leader_tid;
}