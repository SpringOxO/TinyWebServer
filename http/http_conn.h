#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // 包含 struct iovec (分散/聚集 I/O)
#include <arpa/inet.h>   // 包含 sockaddr_in
#include <stdlib.h>
#include <errno.h>
#include <string>
#include <atomic>

#include "../buffer/buffer.h"
#include "http_request.h"
#include "http_response.h"
#include "../router/router.h"

class HttpConn {
public:
    HttpConn();
    ~HttpConn();

    // ----- 生命周期管理 -----
    // 初始化一个新的客户端连接，传入 Socket FD 和客户端 IP 地址
    void Init(int sockFd, const sockaddr_in& addr);
    // 安全关闭当前连接，释放 FD 和映射的内存
    void Close();

    // ----- 核心 I/O 引擎 (与 Epoll 联动) -----
    // 从网卡非阻塞地读取数据，存入 readBuff_
    ssize_t Read(int* saveErrno);
    // 将 writeBuff_ 和 零拷贝的 mmFile_ 非阻塞地轰炸给网卡
    ssize_t Write(int* saveErrno);

    // ----- 🚦 核心调度枢纽 -----
    // 当 Read 完毕后调用此函数，串联 Request -> Router -> Response
    bool Process(HttpRouter& router);

    // 长连接时如何重置request
    void ResetForKeepAlive();

    // ----- 状态获取接口 -----
    int GetFd() const;
    int GetPort() const;
    const char* GetIP() const;
    bool IsKeepAlive() const;
    
    // 还需要给网卡发送多少字节？(Epoll 用它来判断是否要继续监听可写事件)
    int ToWriteBytes() const { return iov_[0].iov_len + iov_[1].iov_len; }

    // ----- 全局静态配置 -----
    static bool isET;             // 是否开启 Epoll 的 ET (边缘触发) 模式
    static std::string srcDir;    // 网站的物理根目录路径
    static std::atomic<int> userCount; // 线程安全的当前在线并发连接数
    

private:
    // 网络基础属性
    int fd_;                      // 客户端的 Socket 描述符
    struct sockaddr_in addr_;     // 客户端的 IP 和端口信息
    bool isClose_;                // 标记该连接是否已经被关闭

    // 分散/聚集 I/O 核心结构 (配合 writev)
    int iovCnt_;                  // 有几个内存块需要发送 (通常是 2 个)
    struct iovec iov_[2];         // iov_[0] 存响应头，iov_[1] 存 mmap 映射的响应体文件

    // 收发缓冲区
    Buffer readBuff_;             // 读缓冲区 (吸收客户端发来的请求报文)
    Buffer writeBuff_;            // 写缓冲区 (存放 HttpResponse 组装好的响应头部)

    // HTTP 核心处理模块
    HttpRequest request_;         // 负责解析 readBuff_
    HttpResponse response_;       // 负责组装响应至 writeBuff_ 和 mmap
};

#endif