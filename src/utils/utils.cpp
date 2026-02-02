#include "utils/utils.hpp"
#include <errno.h>

utilEpoll::utilEpoll()
{
}

utilEpoll::~utilEpoll()
{
}

void utilEpoll::init(int time_slot)
{
    time_slot_ = time_slot;
}

int utilEpoll::setNonBlocking(int fd)
{
    int old_fl_status = fcntl(fd, F_GETFL);         // 获取文件状态
    int new_fl_status = old_fl_status | O_NONBLOCK; // 设置非阻塞
    int ret = fcntl(fd, F_SETFL, new_fl_status);    // 设置文件状态
    if (-1 == ret)
    {
        perror("file status set nonblocking failed!");
        return old_fl_status;
    }
    return ret;
}

// 向内核事件表epoll_fdc注册事件fd，根据one_shot设置EPOLLONESHOT，根据trig_mode设置ET/LT
void utilEpoll::addFd(int epoll_fd, int fd, bool one_shot, int trig_mode)
{
    epoll_event event;
    event.data.fd = fd; // 设置需要注册事件的fd
    // TODO: 是否需要加可以写权限EPOLLOUT
    if (trig_mode == 1)
    { // ET触发
        event.events = EPOLLIN | EPOLLHUP | EPOLLET;
    }
    else
    { // LT触发
        event.events = EPOLLIN | EPOLLHUP;
    }
    if (one_shot)
        event.events |= EPOLLONESHOT;
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    if (-1 == ret)
    {
        // 如果 fd 已经在 epoll 中（EEXIST），改为 MOD 更新事件
        if (errno == EEXIST)
        {
            ret = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
        }
        if (-1 == ret)
        {
            perror("epoll_ctl add/mod fd failed!\n");
            return;
        }
    }
    // 设置fd非阻塞
    setNonBlocking(fd);
}
// 删除事件表上的事件fd
void utilEpoll::deleteFd(int epoll_fd, int fd)
{
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    if (-1 == ret)
    {
        perror("epoll_ctl delete fd failed!\n");
        return;
    }
}

// 调整事件fd为EPOLLONESHOT
void utilEpoll::modFd(int epoll_fd, int fd, int events, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1)
    {
        event.events = events | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    }
    else
    {
        event.events = events | EPOLLONESHOT | EPOLLRDHUP;
    }

    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
    if (-1 == ret)
    {
        perror("epoll_ctl mod fd failed!\n");
        return;
    }
}

// 信号处理函数
void utilEpoll::signalHandler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(util_pipe_fd_[1], (char *)&msg, 1, 0); // 将信号通过写端发送到主线程
    errno = save_errno;
}

// 设置信号函数
void utilEpoll::addSignal(int sig, void(handler)(int), bool restart){
    struct sigaction sa;        
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;   
    if(restart){
        sa.sa_flags |= SA_RESTART;
    } 
    sigfillset(&sa.sa_mask);    // 初始化并填充信号集
    int ret = sigaction(sig, &sa, NULL);
    if(ret == -1){
        perror("sigaction failed");
        return ;
    }
}
