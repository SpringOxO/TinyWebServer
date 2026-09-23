#include "http_conn.h"
#include <bits/types/struct_iovec.h>
#include <sys/types.h>
#include <unistd.h>
#include "../log/log.h"

// 类的全局设置变量
bool HttpConn::isET = true; //这在webserver的InitEventMode_初始化时可能会被修改
std::atomic<int> HttpConn::userCount{0};
std::string HttpConn::srcDir;

HttpConn::HttpConn(){
    isClose_ = true;
}

HttpConn::~HttpConn() {
    Close(); 
}

void HttpConn::Init(int sockFd, const sockaddr_in& addr){
    fd_ = sockFd;
    addr_ = addr;
    isClose_ = false;
    userCount ++;

    readBuff_.RetrieveAll();
    writeBuff_.RetrieveAll();

    // 重要，彻底清空之前的request信息
    request_.Init();
}

void HttpConn::Close(){
    response_.UnmapFile();

    if (isClose_ == false) {
        isClose_ = true;
        userCount--;      // 减少在线人数
        close(fd_);       // 归还句柄
    }
}

ssize_t HttpConn::Read(int* saveErrno){
    //readBuff_.ReadFd(fd_, saveErrno);
    ssize_t len = -1;
    do {
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len <= 0){
            break;
        }
    }
    while (isET);

    return len;
}

// 用writev分段发buff和文件
ssize_t HttpConn::Write(int* saveErrno){
    ssize_t len = -1;
    do {
        len = writev(fd_, iov_, iovCnt_);
        
        // --- 异常处理逻辑 ---
        if (len <= 0) {
            *saveErrno = errno;
            break; // 可能是 EAGAIN (缓冲区满了)，也可能是真正的断开。直接 break，交给上层判断。
        }
        if (len >= iov_[0].iov_len){ // buff发完，文件发了一部分
            size_t offset = len - iov_[0].iov_len; // 文件发送了多少
            iov_[1].iov_base = (uint8_t*)iov_[1].iov_base + offset;
            iov_[1].iov_len -= offset;

            // buff发完了，清空之
            if (iov_[0].iov_len > 0) {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else {
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len;
            iov_[0].iov_len -= len;

            // 记得把发掉的东西从buff中清空
            writeBuff_.Retrieve(len);
        }
        // 都发完了，退出
        if (ToWriteBytes() <= 0) break;
    }
    while (isET);
    return len;
}

bool HttpConn::Process(HttpRouter& router) {
    // ⚠️ 删掉这里的 request_.Init()！它应该在 HttpConn::Init() 里被初始化。
    
    if (readBuff_.ReadableBytes() <= 0) {
        return false;
    }

    // 调用解析模块
    bool parseRet = request_.Parse(readBuff_);
    
    if (parseRet) {
        // ✅ 报文完整且解析成功！去装填 Response
        response_.Init(srcDir, request_.Path(), request_.IsKeepAlive(), 200);
        router.Route(request_, response_);
    } 
    else {
        LOG_INFO("[DEBUG] HttpConn: invalid or incomplete request, continue reading");
        // 报文没收全 (可能是半包)直接返回 false
        return false; 
    }

    LOG_INFO("[DEBUG] HttpConn: full request, making response");

    // 只有完整的报文才需要生成响应
    response_.MakeResponse(writeBuff_);
    
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    if (response_.FileLen() > 0){
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    } else {
        // 防御野指针
        iov_[1].iov_base = nullptr;
        iov_[1].iov_len = 0;
    }
    
    return true;
}

void HttpConn::ResetForKeepAlive() {
    // 1. 清空上一个请求的解析状态机
    request_.Init();
    
    // 2. 解除上一个请求映射的网页文件（极其关键，否则会内存泄漏或野指针）
    response_.UnmapFile();
    
    // 3. 清空写缓冲区（注意：绝对不能清空 readBuff_）
    writeBuff_.RetrieveAll();
    
    // 4. 将发货标记清零
    iovCnt_ = 0;
}

int HttpConn::GetFd() const{
    return fd_;
}

int HttpConn::GetPort() const{
    return ntohs(addr_.sin_port);
}

const char * HttpConn::GetIP() const{
    return inet_ntoa(addr_.sin_addr);
}

bool HttpConn::IsKeepAlive() const{
    return request_.IsKeepAlive();
}