#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map>
#include <string>
#include <sys/stat.h>    // 包含获取文件状态的结构体 struct stat
#include <sys/mman.h>    // 包含 mmap, munmap 等零拷贝内存映射函数

#include "../buffer/buffer.h"

class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    // 初始化响应对象（为了 Keep-Alive 长连接复用）
    // srcDir: 网站根目录；path: 请求的相对路径；isKeepAlive: 是否保持连接；code: 状态码
    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    
    // 核心引擎：生成 HTTP 响应报文，并将文本部分写入 buff 中
    void MakeResponse(Buffer& buff);

    // 给router使用的 API，用于直接设置内存字符串作为响应体
    void SetContent(const std::string& body, const std::string& type);
    
    // 安全释放 mmap 映射的文件内存
    void UnmapFile();
    
    // ----- 获取内存映射文件信息的接口 (供 HttpConn 调用 writev 发送) -----
    char* File();
    size_t FileLen() const;
    std::string SrcDir() const{return srcDir_;}
    
    // 如果发生错误，可以由外部强制修改状态码
    void ErrorContent(Buffer& buff, std::string message);
    int Code() const { return code_; }

private:
    // ----- 报文拼装的内部子状态函数 -----
    void AddStateLine_(Buffer& buff); // 添加响应行 (如 HTTP/1.1 200 OK)
    void AddHeader_(Buffer& buff);    // 添加头部字段 (如 Content-Length)
    void AddContent_(Buffer& buff);   // 添加响应体或进行文件 mmap 映射

    
    // ----- 辅助函数 -----
    void ErrorHtml_();                // 遇到 404/403 等错误时，自动重定向到错误提示页面
    std::string GetFileType_();       // 根据文件后缀推导 Content-Type (如 .html -> text/html)

    // ----- 核心状态数据 -----
    int code_;                        // HTTP 状态码
    bool isKeepAlive_;                // 是否保持长连接
    std::string path_;                // 请求文件的路径
    std::string srcDir_;              // 网站的根目录路径

    // 用来存放动态生成的 JSON / 纯文本
    std::string body_; 
    // 用来覆盖默认的 Content-Type (比如 application/json)
    std::string type_;
    
    // ----- 零拷贝核心数据结构 -----
    char* mmFile_;                    // 指向 mmap 映射后的内存首地址
    struct stat mmFileStat_;          // 保存请求文件的状态信息 (如文件大小、是否为目录)

    // ----- 静态字典：用来替代原代码里的硬编码 if-else -----
    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE; // 后缀名 -> MimeType
    static const std::unordered_map<int, std::string> CODE_STATUS;         // 状态码 -> 描述文字 (200 -> OK)
    static const std::unordered_map<int, std::string> CODE_PATH;           // 状态码 -> 错误网页路径 (404 -> /404.html)
};

#endif