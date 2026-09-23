#include "buffer.h"
#include <algorithm>
#include <bits/types/struct_iovec.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <sys/uio.h>
#include <unistd.h>
#include <cassert>

Buffer::Buffer(int initBuffSize){
    if (initBuffSize <= 0) initBuffSize = 0;
    buffer_.resize(initBuffSize);
    readPos_ = 0;
    writePos_ = 0;
}

size_t Buffer::WritableBytes() const{
    return buffer_.size() - writePos_;
}

size_t Buffer::ReadableBytes() const{
    return writePos_ - readPos_;
}

size_t Buffer::PrependableBytes() const{
    return readPos_;
}

const char* Buffer::Peek() const{
    return buffer_.data() + readPos_;
}

const char* Buffer::BeginWrite() const{
    return buffer_.data() + writePos_;
}

void Buffer::MakeSpace_(size_t len) {
    if (WritableBytes() + PrependableBytes() < len) {
        // 空闲空间不足，真扩容
        buffer_.resize(writePos_ + len + 1);
    } else {
        size_t readable = ReadableBytes();
        
        // 使用 std::copy 把有用数据往前平移，覆盖掉前面的废弃空间
        std::copy(BeginPtr_() + readPos_, 
                  BeginPtr_() + writePos_, 
                  BeginPtr_());
        
        readPos_ = 0;
        writePos_ = readPos_ + readable; 
    }
}

void Buffer::Append(const std::string& str){
    Buffer::Append(str.data(), str.length());
}

void Buffer::Append(const char* data, size_t len){
    if (WritableBytes() < len){
        MakeSpace_(len);
    }
    std::copy(data, data + len, buffer_.data() + writePos_);
    writePos_ += len;
}

void Buffer::Retrieve(size_t len) {
    if (len < ReadableBytes()) {
        readPos_ += len; // 数据没读完，只移动读指针
    } else {
        RetrieveAll();   // 数据全读完了，触发清零机制！
    }
}

// 基本正确但有两个问题：没加清零机制；判断条件虽然数学上对但不够直接，指针和整数运算会推导为有符号整数，而size_t是无符号整数，对一些极端故障场景会有问题
// void Buffer::RetrieveUntil(const char* end) {
//     if (end - buffer_.data() - readPos_ <= ReadableBytes()){
//         readPos_ = end - buffer_.data();
//     }
// }
// 改进版：
void Buffer::RetrieveUntil(const char* end) {
    // 1. 极其严苛的安全防御：确保 end 绝对在可读数据的有效内存范围内
    assert(Peek() <= end);
    assert(end <= BeginWrite());
    
    // 2. 完美的复用：直接算出这段距离的长度，交给写好的 Retrieve 去处理
    // 这样既移动了指针，又完美触发了 Retrieve 里的“归零回收”机制
    Retrieve(end - Peek());
}

void Buffer::RetrieveAll(){
    readPos_ = 0;
    writePos_ = 0;
}

std::string Buffer::RetrieveAllToStr(){
    std::string str = std::string(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

// 为了获得最好性能（尽量不扩展buffer），使用buffer和临时buf来分块接收网卡信息（单次接收最多64k）
ssize_t Buffer::ReadFd(int fd, int* saveErrno){
    char extrabuf[65536];
    struct iovec iov[2];
    const size_t writable = WritableBytes();
    // 第一块
    iov[0].iov_base = buffer_.data() + writePos_;
    iov[0].iov_len = writable;
    // 第二块
    iov[1].iov_base = extrabuf;
    iov[1].iov_len = sizeof(extrabuf);

    // 如果可写空间已经65536了就不用第二块buf
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t len = readv(fd, iov, iovcnt);

    if (len < 0){
        *saveErrno = errno; // 保存错误码
    }
    else if (len <= writable){
        writePos_ += len;
    }
    else {
        writePos_ += writable;
        Append(extrabuf, len - writable);
    }
    return len;
}

ssize_t Buffer::WriteFd(int fd, int* saveErrno){
    size_t readable = ReadableBytes();
    ssize_t len = write(fd, Peek(), readable);

    if (len < 0){
        *saveErrno = errno;
    }
    else {
        //RetrieveAll(); // 不能retrieveall！！！因为可能一次发不完，严格发多少清多少
        Retrieve(len);;
    }
    return len;
}