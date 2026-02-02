#include "threadpool/thread_set.hpp"

ThreadSet::ThreadSet()
    : state_(ThreadState::FOLLOWER),
      has_leader_(false),
      leader_tid_() {}

ThreadSet::~ThreadSet() = default;

bool ThreadSet::join() {
    mutex_.lock();

    const pthread_t self = pthread_self();

    // 没有 leader：直接接任
    if (!has_leader_) {
        has_leader_ = true;
        leader_tid_ = self;
        state_ = ThreadState::LEADER;
        mutex_.unlock();
        return true;
    }

    // 有 leader：入 followers 并等待被推举
    followers_.insert(self);
    state_ = ThreadState::FOLLOWER;

    while (has_leader_ && leader_tid_ != self) {
        leader_cond_.wait(*mutex_.get());

        // 被唤醒后，如果此时没有 leader，则抢占
        if (!has_leader_) {
            has_leader_ = true;
            leader_tid_ = self;
            state_ = ThreadState::LEADER;
            followers_.erase(self);
            mutex_.unlock();
            return true;
        }
    }

    mutex_.unlock();
    return false;
}

void ThreadSet::promote_new_leader() {
    mutex_.lock();

    // 当前 leader 将处理事件，自己转为 Processing，同时让出 leader 位置
    state_ = ThreadState::PROCESSING;
    has_leader_ = false;
    leader_tid_ = pthread_t();

    // 唤醒一个 follower 竞争成为新 leader
    if (!followers_.empty()) {
        leader_cond_.signal();
    }

    mutex_.unlock();
}

void ThreadSet::transition_state() {
    mutex_.lock();

    const pthread_t self = pthread_self();

    // 事件处理完成：如果此时没有 leader，直接接任
    if (!has_leader_) {
        has_leader_ = true;
        leader_tid_ = self;
        state_ = ThreadState::LEADER;
        followers_.erase(self);
        mutex_.unlock();
        return;
    }

    // 否则回到 follower
    followers_.insert(self);
    state_ = ThreadState::FOLLOWER;
    mutex_.unlock();
}

void ThreadSet::set_state(ThreadState state) {
    mutex_.lock();
    state_ = state;
    mutex_.unlock();
}

pthread_t ThreadSet::has_leader() {
    mutex_.lock();
    const pthread_t tid = has_leader_ ? leader_tid_ : pthread_t();
    mutex_.unlock();
    return tid;
}