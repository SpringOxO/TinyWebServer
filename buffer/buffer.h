#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <string>
#include <atomic>
#include <sys/uio.h> // for readv

class Buffer {
public:
    // 默认初始化 1024 字节
    Buffer(int initBuffSize = 1024);
    ~Buffer() = default;

    // 可读字节数
    size_t WritableBytes() const;
    // 可写字节数
    size_t ReadableBytes() const;
    // 读指针前空闲字节数
    size_t PrependableBytes() const;

    // 获取读指针具体位置
    const char* Peek() const; // 用于给 HTTP 解析器看数据
    const char* BeginWrite() const;

    // 追加数据
    void Append(const std::string& str);
    void Append(const char* data, size_t len);

    // 消耗数据（指针后移）
    void Retrieve(size_t len);
    void RetrieveUntil(const char* end);
    void RetrieveAll(); // 清空缓冲区

    // 将缓冲区的数据转为 string 拿出来
    std::string RetrieveAllToStr();

    // 最核心的 I/O 操作
    ssize_t ReadFd(int fd, int* saveErrno);
    ssize_t WriteFd(int fd, int* saveErrno);

private:
    char* BeginPtr_(){ return &*buffer_.begin(); }; // 返回底层的起始地址
    const char* BeginPtr_() const{ return &*buffer_.begin(); };
    void MakeSpace_(size_t len); // 内部扩容函数

    std::vector<char> buffer_; // 彻底替代 char 数组
    std::atomic<std::size_t> readPos_;
    std::atomic<std::size_t> writePos_;
};

#endif