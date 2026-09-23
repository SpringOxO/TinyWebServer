#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory>

#include "epoller/epoller.h"       // 假设：封装了 epoll_create/epoll_wait 的现代类
#include "threadpool/threadpool.h"    // 假设：现代 C++ 实现的线程池
#include "http/http_conn.h"
#include "router/router.h"
#include "CGImysql/sql_connection_pool.h"
#include "log/log.h"

class WebServer {
public:
    // 构造函数：集齐所有配置参数 (端口、触发模式、超时时间、线程数、数据库配置等)
    WebServer(
        int port, int trigMode, int timeoutMS, int optLinger,
        int threadNum, int sqlNum, int sqlPort, const char* sqlUser, 
        const char* sqlPwd, const char* dbName
    );
    
    // 析构函数：释放端口，关闭 Epoll
    ~WebServer();

    // ----- 🚦 核心大循环：服务器启动引擎 -----
    void Start();

private:
    // ----- 第一阶段：基建与初始化 -----
    bool InitSocket_();             // 绑定 IP 和端口，开启监听
    void InitEventMode_(int trigMode); // 初始化 Epoll 的 ET/LT 触发模式
    void InitRouter_();             // 在这里用 Lambda 注册所有的 GET/POST 业务路由

    // ----- 第二阶段：主线程的“派发”逻辑 (交警指挥交通) -----
    void HandleListen_();           // 雷达发现新连接：执行 accept 并注册到 Epoll
    void HandleRead_(HttpConn* client);  // 雷达发现可读：将读任务打包扔进线程池
    void HandleWrite_(HttpConn* client); // 雷达发现可写：将写任务打包扔进线程池

    // ----- 第三阶段：工作线程的“执行”逻辑 (工人干活) -----
    // 这些函数是在 ThreadPool 中的子线程里被调用的！
    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);

    // ----- 连接与状态管理 -----
    void SendError_(int fd, const char* info); // 连接极其异常时的底层报错
    void CloseConn_(HttpConn* client);         // 安全关闭连接，从 Epoll 和字典中剔除

    // ----- 服务器基础配置 -----
    int port_;
    bool isClose_;                 // 服务器是否正在关闭
    int timeoutMS_;                // 客户端多久不发数据就被踢掉
    uint32_t listenEvent_;         // Listen Socket 的 Epoll 触发模式 (如 EPOLLIN)
    uint32_t connEvent_;           // Client Socket 的 Epoll 触发模式 (如 EPOLLIN | EPOLLONESHOT | EPOLLET)
    int listenFd_;                 // 服务器监听大门的 Socket 描述符
    int optLinger_;                // 优雅关闭配置选项 (0 表示关，1 表示开)

    // ----- 核心子系统 (使用 unique_ptr 绝对避免内存泄漏) -----
    std::unique_ptr<Epoller> epoller_;
    std::unique_ptr<ThreadPool> threadpool_;
    
    // ----- 路由中心 -----
    HttpRouter router_;

    // ----- 👥 全局客户端对象池 -----
    // 这是一个极其关键的数据结构！
    // 将底层的文件描述符 (FD) 映射到高层的 HttpConn 对象
    std::unordered_map<int, HttpConn> users_;
};

#define MAX_FD 20000 //最大连接人数

#endif