#include "utils/timer.hpp"

bool cmp(const util_timer *a, const util_timer *b)
{
    return a->expire < b->expire;
}

// 添加定时器
void conn_timer_manager::add_timer(util_timer *timer)
{
    if (!timer)
        return;

    // 将定时器添加到定时器管理器中
    timers_list_.push_back(timer);
    resort_timer(timer); // 按照超时时间升序排列，其实就是按照连接先后的时间来
}

// 删除定时器
void conn_timer_manager::del_timer(util_timer *timer)
{
    if (!timer)
        return;

    // 从定时器管理器中删除定时器
    timers_list_.remove(timer);
}

// 重排序定时器
void conn_timer_manager::resort_timer(util_timer *timer)
{
    timers_list_.sort(cmp);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void conn_timer_manager::timer_handler()
{
    tick();
    alarm(time_slot);
}

// 周期性清理已到期的定时器
void conn_timer_manager::tick()
{
    if (timers_list_.empty())
        return;

    time_t cur = time(nullptr); // 获取当前时间
    auto it = timers_list_.begin();
    while (it != timers_list_.end())
    {
        util_timer *timer = *it;

        if (timer->expire > cur)
        {
            break; // 当前定时器超时时间大于当前时间，表明没有超时，退出循环
        }

        // 日志输出
        LOG_INFO("%s", ("[conn_timer_manager::tick] expire now, fd=" +
                std::to_string(timer && timer->user_data_ ? timer->user_data_->sockfd : -1) +
                ", expire=" + std::to_string(timer->expire) +
                ", now=" + std::to_string(cur))
                .c_str());
        
        if (timer->cb_func_)
        {
            timer->cb_func_(timer->user_data_);
        }

        it = timers_list_.erase(it);
        delete timer;
    }
}

void conn_timer_manager::print_timer(){
    auto it = timers_list_.begin();
    LOG_INFO("%s", "[conn_timer_manager::print_timer] current timer_manager ");
    while(it != timers_list_.end()){
        util_timer *timer = *it;
        LOG_INFO("%s", ("-----fd = " + std::to_string(timer->user_data_->sockfd) + ", expire = " + std::to_string(timer->expire)).c_str());
        it++;
    }
}
