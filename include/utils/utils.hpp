#ifndef UTILS_HPP
#define UTILS_HPP

#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <signal.h>
class utilEpoll
{
public:
    utilEpoll();
    ~utilEpoll();

    void init(int time_slot);

    int setNonBlocking(int fd);

    void addFd(int epoll_fd, int fd, bool one_shot, int trig_mode);

    void deleteFd(int epoll_fd, int fd);

    void modFd(int epoll_fd, int fd, int events, int TRIGMode);

    void signalHandler(int sig);

    void addSignal(int sig, void(handler)(int), bool restart);

public:
    int time_slot_;

    int util_ep_fd_;          // epoll文件描述符 
    int* util_pipe_fd_;     // 将信号安全的转发到主线程的epoll循环 
};

#endif // UTILS_HPP