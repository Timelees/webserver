// L/F模式的句柄集组件
#ifndef HANDLE_SET_HPP
#define HANDLE_SET_HPP

#include <sys/epoll.h>
#include <functional>
#include <vector>
#include <unordered_map>
#include "lock/locker.hpp"

class EventHandler;

class HandleSet{
public:
    HandleSet(int max_events = 1024);
    ~HandleSet();

    /**
     * @brief 监听句柄上的IO事件
     * @param timeout 超时时间(ms)，-1表示阻塞等待
     * @return 就绪事件数量
     */
    int wait_for_event(int timeout = -1);

    /**
     * @brief 将句柄和时间处理器绑定
     * @param fd 需要监听的文件描述符
     * @param handler 处理函数
     * @param events 监听的事件类型(EPOLLIN/EPOLLOUT等)
     */
    void register_handle(int fd, EventHandler* handler, uint32_t events);
    
    /**
     * @brief 注销句柄
     */
    void unregister_handle(int fd);

    /**
     * @brief 获取就绪事件中的文件描述符
     * @param index 事件索引
     * @return 事件处理器指针和事件类型
     */
    std::pair<EventHandler*, uint32_t> get_ready_event(int index);

    int get_epoll_fd() const{ return epoll_fd_; };

private:
    int epoll_fd_;                  // epoll文件描述符
    int max_events_count_;          // 最大事件数量
    std::vector<struct epoll_event> events_;    // 就绪事件数组
    std::unordered_map<int, EventHandler*> handlers_;   // 句柄与事件处理器的映射
    MutexLock mutex_;
};


#endif // HANDLE_SET_HPP