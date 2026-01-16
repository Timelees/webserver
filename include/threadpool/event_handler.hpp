// include/threadpool/event_handler.hpp
#ifndef EVENT_HANDLER_HPP
#define EVENT_HANDLER_HPP
#include <cstdint>

class Handle;  // 可以是fd的封装

class EventHandler {
public:
    virtual ~EventHandler() = default;
    
    /**
     * @brief 获取关联的句柄
     */
    virtual int get_handle() const = 0;
    
    /**
     * @brief 处理事件的回调函数
     * @param events 触发的事件类型
     */
    virtual void handle_event(uint32_t events) = 0;
};

#endif