#include "http_request.h"
#include <algorithm>
#include "../log/log.h"

void HttpRequest::Init (){
    state_ = ParseState::REQUEST_LINE;  

    // 2. 清空所有上一次解析出的字符串残留
    method_ = path_ = version_ = body_ = "";

    headers_.clear();
    postData_.clear();
}

std::string HttpRequest::Method () const{
    return method_;
}

std::string HttpRequest::Path () const{
    return path_;
}

std::string& HttpRequest::Path (){
    return path_;
}

std::string HttpRequest::Version () const{
    return version_;
}

std::string HttpRequest::GetPost (const std::string& key) const{
    if(postData_.count(key) == 1) {
        return postData_.at(key);
    }
    return "";
}

std::string HttpRequest::GetHeader (const std::string& key) const{
    if(headers_.count(key) == 1) {
        return headers_.at(key);
    }
    return "";
}

bool HttpRequest::Parse(Buffer& buff) {
    const char CRLF[] = "\r\n"; // HTTP 协议的换行符标准
    
    if (buff.ReadableBytes() <= 0) {
        return false;
    }

    // 主状态机：不断从 Buffer 剥离文本行，直到解析完成
    while (buff.ReadableBytes() && state_ != ParseState::FINISH) {
        //对于body的处理逻辑
        if (state_ == ParseState::BODY) {
            int contentLen = 0;
            // 安全获取 Content-Length
            if (headers_.count("Content-Length") == 1) {
                contentLen = std::stoi(headers_.at("Content-Length"));
            } else {
                // 如果是没带 Content-Length 的异常 POST，直接强制结束
                state_ = ParseState::FINISH;
                break; 
            }

            // 判断当前 Buffer 里的数据够不够 Content-Length 的长度
            if (buff.ReadableBytes() < contentLen) {
                return false; // TCP 确实没发完，半包，返回等待
            }

            // 数据终于够了！一口气把整个 Body 挖出来
            std::string bodyStr(buff.Peek(), contentLen);
            ParseBody_(bodyStr); // 调用你现成的 ParseBody_，它会设置 state_ = FINISH
            
            buff.Retrieve(contentLen); // 把读完的 Body 从缓冲区丢弃
            break; // 状态已经 FINISH，大功告成，跳出循环！
        }

        // 1. 获取一行数据：在 Buffer 中寻找最近的 \r\n（但对于body不能这样，不一定如此结尾）
        const char* lineEnd = std::search(buff.Peek(), buff.BeginWrite(), CRLF, CRLF + 2);
        
        // 如果没找到 \r\n，说明网卡发来的数据被切断了（TCP 拆包），结束解析，等下次数据到来
        if (lineEnd == buff.BeginWrite()) {
            return false; 
        }

        // 2. 转化为 std::string
        std::string line(buff.Peek(), lineEnd);
        LOG_INFO("[DEBUG] HttpRequest: parsingline: %s", line.data());

        // 3. 状态机路由分发
        switch (state_) {
            case ParseState::REQUEST_LINE:
                if (!ParseRequestLine_(line)) return false;
                break;
            case ParseState::HEADERS:
                ParseHeader_(line);

                if (line.empty()) { // 头部结束
                    if (method_ == "POST") {
                        // POST 请求：必须进入 BODY 状态，继续把后面的表单读完
                        state_ = ParseState::BODY;
                    } else {
                        // GET/HEAD 请求：没有请求体，解析到此完美结束
                        state_ = ParseState::FINISH; 
                    }
                }
                break;
            case ParseState::BODY:
                ParseBody_(line);
                break;
            default:
                break;
        }

        // 4. 解析完一行，命令 Buffer 将其消耗掉 (别忘了 +2 跳过 \r\n)
        if (lineEnd == buff.BeginWrite()) break;
        buff.RetrieveUntil(lineEnd + 2); 
    }
    // 是否已经读完
    return state_ == ParseState::FINISH;
}

bool HttpRequest::ParseRequestLine_(const std::string& line) {
    // 1. 制定匹配规则：寻找三个被空格隔开的字符串，且最后包含 HTTP/
    // R"(...)" 是 C++11 的原始字符串字面量，写正则不用痛苦地转义反斜杠
    std::regex pattern(R"(^([^ ]*) ([^ ]*) HTTP/([^ ]*)$)");
    std::smatch subMatch;

    // 2. 尝试匹配这行文本
    if (std::regex_match(line, subMatch, pattern)) {
        // subMatch[0] 是整个字符串，1、2、3 分别对应括号里捕获的内容
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];

        // 3. 转移状态
        state_ = ParseState::HEADERS; 
        return true;
    }
    
    // 如果格式不符，直接返回 false 拒绝请求
    return false; 
}

void HttpRequest::ParseHeader_ (const std::string& line) {
    size_t colon_pos = line.find(':'); //找冒号的位置
    
    if (colon_pos != std::string::npos) {
        // key
        std::string key = line.substr(0, colon_pos);
        // value
        std::string value = line.substr(colon_pos + 1);
        
        // 细节优化：HTTP 协议标准中，冒号和 Value 之间通常会有一个空格 (例如 "Host: 127.0.0.1")
        // 我们需要把这个干扰性的前导空格去掉
        size_t value_start = value.find_first_not_of(" \t"); // 找第一个不是空格或制表符的位置
        if (value_start != std::string::npos) {
            value = value.substr(value_start);
        } else {
            value = ""; // 说明冒号后面全是空格，值为空
        }
        
        headers_[key] = value;
    }
}

void HttpRequest::ParseBody_ (const std::string& line){
    body_ += line;
    ParsePost_();
    state_ = ParseState::FINISH;
}

void HttpRequest::ParsePost_() {
    // 安全防御：只处理 POST 请求，并且 Content-Type 必须是表单格式
    if (method_ == "POST" && GetHeader("Content-Type") == "application/x-www-form-urlencoded") {
        if (body_.empty()) return;

        size_t start = 0;
        // 第一层循环：通过 '&' 切割不同的键值对 (如 "user=admin" 和 "password=123")
        while (start < body_.size()) {
            size_t end = body_.find('&', start);
            if (end == std::string::npos) {
                end = body_.size(); // 到了字符串末尾
            }

            std::string kv = body_.substr(start, end - start);
            
            // 第二次切割：通过 '=' 分离 Key 和 Value
            size_t equal_pos = kv.find('=');
            if (equal_pos != std::string::npos) {
                // 提取并立刻进行 URL 解码
                std::string key = UrlDecode(kv.substr(0, equal_pos));
                std::string value = UrlDecode(kv.substr(equal_pos + 1));
                
                // 存入专用的表单哈希表中
                postData_[key] = value; 
            }
            start = end + 1; // 移动到下一个键值对的开头
        }
    }
}

int HttpRequest::ConverHex(char ch) {
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return ch - '0';
}

// 解码url中的特殊字符（+和以%AA形式表示的中文）
std::string HttpRequest::UrlDecode(const std::string& str) {
    std::string result;
    // 提前预留空间，避免频繁分配内存
    result.reserve(str.size()); 
    
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '+') {
            // URL 编码中的 '+' 代表空格
            result += ' ';
        } 
        else if (str[i] == '%' && i + 2 < str.length()) {
            // 遇到 '%'，将后面两个十六进制字符还原为原始字节
            char high = str[i + 1];
            char low = str[i + 2];
            char decoded_char = static_cast<char>(ConverHex(high) * 16 + ConverHex(low));
            
            result += decoded_char;
            i += 2; // 跳过被消耗的两个十六进制字符
        } 
        else {
            // 普通字符直接拷贝
            result += str[i];
        }
    }
    return result;
}

bool HttpRequest::IsKeepAlive() const {
    std::string connection = GetHeader("Connection");
    // HTTP/1.1 默认就是 keep-alive，除非明确写了 close
    if (connection == "close") {
        return false;
    }
    if (version_ == "1.1") {
        return true;
    }
    return connection == "keep-alive";
}