#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <regex>
#include <unordered_map>
#include "../buffer/buffer.h"

class HttpRequest {
public:

    enum class ParseState {
        REQUEST_LINE, // 正在解析请求行 (如 GET / HTTP/1.1)
        HEADERS,      // 正在解析头部 (如 Host: 127.0.0.1)
        BODY,         // 正在解析请求体 (POST 请求的表单数据)
        FINISH        // 解析完成
    };

    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    // 清空上次解析遗留的数据（为了完美支持 HTTP Keep-Alive 长连接）
    void Init(); 

    // 核心解析引擎
    // 返回 true 表示成功解析出一个完整的 HTTP 请求
    // 返回 false 表示 Buffer 里的数据还不够，需要等 Epoll 继续读网卡
    bool Parse(Buffer& buff); 

    // ----- 优雅的 Getter 接口 -----
    std::string Path() const;
    std::string& Path();           // 返回引用，方便 Router 重写路径 (比如把 "/" 变成 "/index.html")
    std::string Method() const;
    std::string Version() const;
    std::string GetPost(const std::string& key) const;   // 快速获取 POST 表单里的账号密码
    std::string GetHeader(const std::string& key) const; // 快速查询头部字段

    bool IsKeepAlive() const;      // 判断是否需要保持连接

private:
    // 内部子状态处理函数
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void ParseBody_(const std::string& line);

    void ParsePost_();             // 专门处理 application/x-www-form-urlencoded

    std::string UrlDecode(const std::string& str);
    
    // 提取一行文本的辅助函数 (替代原代码里繁琐的 parse_line)
    static int ConverHex(char ch); // 辅助函数：用于 URL 字符解码 (%20 转空格)

    // ----- 核心数据容器 -----
    ParseState state_;
    std::string method_, path_, version_, body_;
    
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> postData_; 
};

#endif