#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> // epoll_ctl, epoll_wait
#include <unistd.h>    // close
#include <vector>

class Epoller {
public:
    // 构造时默认创建一个大小为 1024 的事件 vector
    explicit Epoller(int maxEvent = 1024);
    
    // RAII：析构时自动 close(epollFd_)
    ~Epoller();

    // 核心接口：取代原来晦涩的 epoll_ctl 调用
    bool AddFd(int fd, uint32_t events);
    bool ModFd(int fd, uint32_t events);
    bool DelFd(int fd);

    // 核心接口：阻塞等待事件发生，返回触发的事件数量
    int Wait(int timeoutMs = -1);

    // 安全获取事件的接口，消灭底层的 events[i].data.fd
    int GetEventFd(size_t i) const;
    uint32_t GetEvents(size_t i) const;

private:
    int epollFd_; // epoll_create 返回的内核事件表句柄

    // 替代原项目中写死的 epoll_event events[MAX_EVENT_NUMBER];
    // 使用 std::vector 支持动态扩容
    std::vector<struct epoll_event> events_; 
};

#endif