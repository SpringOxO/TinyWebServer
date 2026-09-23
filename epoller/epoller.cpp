#include "Epoller.h"
#include <assert.h>

// 创建 epoll 句柄并初始化事件数组
Epoller::Epoller(int maxEvent) : epollFd_(epoll_create(512)), events_(maxEvent) {
    // 确保内核成功创建了 epoll 对象
    assert(epollFd_ >= 0 && events_.size() > 0); 
}

// 析构函数：RAII 机制，对象销毁时自动关闭底层 fd
Epoller::~Epoller() {
    close(epollFd_);
}

// 添加要监听的 FD 及其事件
bool Epoller::AddFd(int fd, uint32_t events) {
    if (fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    // EPOLL_CTL_ADD: 注册新的 fd 到 epoll 中
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

// 修改已经监听的 FD 的事件 (比如从 读 改为 写)
bool Epoller::ModFd(int fd, uint32_t events) {
    if (fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    // EPOLL_CTL_MOD: 修改已经注册的 fd 的监听事件
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

// 将 FD 从监听树上摘除
bool Epoller::DelFd(int fd) {
    if (fd < 0) return false;
    epoll_event ev = {0};
    // EPOLL_CTL_DEL: 从 epoll 中删除该 fd
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);
}

// 核心等待函数：交出 CPU 控制权，直到有事件发生或超时
int Epoller::Wait(int timeoutMs) {
    // epoll_wait 会将发生事件的 fd 填充到 events_.data() 数组中
    // 返回值是触发事件的 fd 数量
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

// 安全获取事件的 FD
int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}

// 安全获取事件的具体类型 (EPOLLIN, EPOLLOUT 等)
uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}