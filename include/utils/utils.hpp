#ifndef UTILS_HPP
#define UTILS_HPP

#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <strings.h>
#include <stdio.h>


class utilEpoll{
public:
    utilEpoll();
    ~utilEpoll();

    void init(int time_slot);

    int setNonBlocking(int fd);

    void addFd(int epoll_fd, int fd, bool one_shot, int trig_mode);

    void deleteFd(int epoll_fd, int fd);

    void modFd(int epoll_fd, int fd, int events, int TRIGMode);

public:
    int time_slot_;
};

#endif  // UTILS_HPP