#include "http_response.h"
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <sys/stat.h>
#include "../log/log.h"

const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" }
};

HttpResponse::HttpResponse() {
    code_ = -1;
    path_ = "";
    srcDir_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr;    // 极其关键：必须初始化为空，否则析构时会释放野指针
    mmFileStat_ = {0};    // 清空 stat 结构体
}

HttpResponse::~HttpResponse() {
    UnmapFile(); 
}

void HttpResponse::Init(const std::string& srcDir, std::string& path, bool isKeepAlive, int code){
    if (mmFile_) { 
        UnmapFile(); // 内部会调用 munmap 并将 mmFile_ 置为 nullptr
    }
    srcDir_ = srcDir;
    path_ = path;
    isKeepAlive_ = isKeepAlive;
    code_ = code;

    body_.clear();
    type_.clear();

    mmFileStat_ = {0};
}

void HttpResponse::MakeResponse(Buffer& buff){
    if (!body_.empty()) {
        AddStateLine_(buff);
        AddHeader_(buff);
        AddContent_(buff);
        return; 
    }

    std::string fullPath = srcDir_ + path_;
    if (stat(fullPath.data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)){
        code_ = 404;
    }
    else if (!(mmFileStat_.st_mode & S_IROTH)) {
        code_ = 403;
    }
    else if (code_ == -1){ // 如果init指定了code就不改它
        code_ = 200;
    }

    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}

void HttpResponse::AddStateLine_(Buffer &buff){
    std::string status;
    if (CODE_STATUS.count(code_) == 1){
        status = CODE_STATUS.at(code_);
    }
    else {
        code_ = 400; //未知状态码
        status = CODE_STATUS.at(400);
    }

    buff.Append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}

void HttpResponse::AddHeader_(Buffer& buff) {
    // 写入连接状态
    buff.Append("Connection: ");
    if (isKeepAlive_) {
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n"); // 告诉浏览器多久后断开
    } else {
        buff.Append("close\r\n");
    }
    
    // 如果没指定type，根据文件后缀名 (GetFileType_) 生成 Content-Type
    if (!type_.empty()) {
        buff.Append("Content-Type: " + type_ + "\r\n");
    } else {
        // 如果 type_ 是空的，说明这是请求静态文件，才去推导后缀名
        buff.Append("Content-Type: " + GetFileType_() + "\r\n");
    }
}

void HttpResponse::AddContent_(Buffer& buff) {
    // A:动态内容（json）
    if (!body_.empty()) {
        LOG_INFO("[DEBUG] Respongse json!!! content: %s", body_.data());
        // 直接算出字符串大小作为 Content-Length
        buff.Append("Content-Length: " + std::to_string(body_.size()) + "\r\n");
        // 写入空行，标志头部结束
        buff.Append("\r\n");
        // 把动态生成的 JSON/文本 追加到发送缓冲区中！
        buff.Append(body_);
        return; // 动态内容处理完毕，直接返回！不涉及任何文件操作！
    }

    // B:静态文件
    int srcFd = open((srcDir_ + path_).data(), O_RDONLY);
    
    // 万一文件打开失败（比如刚才还在，突然被删了），动态生成一段报错文本发过去
    if (srcFd < 0) {
        LOG_ERROR("Open file failed");
        ErrorContent(buff, "File NotFound!");
        return; 
    }

    // 1. 极其关键的 Content-Length，没有它浏览器就会一直转圈等待
    buff.Append("Content-Length: " + std::to_string(mmFileStat_.st_size) + "\r\n");
    
    // 2. 写入空行！标志着纯文本头部的彻底结束
    buff.Append("\r\n");

    // 3. 终极魔法：mmap 零拷贝映射！
    // 将磁盘上的文件直接映射到内存中 (MAP_PRIVATE 表示只读不写)
    // 映射得到的指针 mmFile_ 之后将交由 HttpConn 中的 writev 函数直接发给网卡
    int* mmRet = (int*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if (*mmRet == -1) { // 映射失败的防御
        LOG_ERROR("File map failed");
        ErrorContent(buff, "File NotFound!");
        return; 
    }
    
    mmFile_ = (char*)mmRet;
    
    // 映射完了，文件句柄就可以关闭了（内存映射依然有效，直到我们手动 munmap）
    close(srcFd); 
}

std::string HttpResponse::GetFileType_() {
    std::string::size_type idx = path_.find_last_of('.');
    if(idx == std::string::npos) return "text/plain";
    std::string suffix = path_.substr(idx);
    if(suffix == ".html") return "text/html";
    if(suffix == ".jpg") return "image/jpeg";
    if(suffix == ".ico") return "image/x-icon";
    // ... (根据需要添加其他如 .css, .js 等)
    return "text/plain";
}

void HttpResponse::SetContent(const std::string& body, const std::string& type) {
    body_ = body;
    type_ = type;
}

void HttpResponse::UnmapFile() {
    if (mmFile_) {
        munmap(mmFile_, mmFileStat_.st_size);
        // 释放后立刻将指针置空，防止越界
        mmFile_ = nullptr;
    }
}

char* HttpResponse::File(){
    return mmFile_;
}

size_t HttpResponse::FileLen() const{
    return mmFileStat_.st_size;
}

// 生成错误页面
void HttpResponse::ErrorContent(Buffer& buff, std::string message) {
    std::string body;
    std::string status;

    // 1. 安全提取状态码对应的描述语 (如 "Not Found")
    if (CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.at(code_);
    } else {
        status = "Bad Request";
    }

    // 2. 动态拼接一个基础的 HTML 错误提示页面
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    
    // 粗体居中显示状态码和描述 (如 "404 : Not Found")
    body += "<h1 align=\"center\">" + std::to_string(code_) + " : " + status + "</h1>\n";
    
    // 显示具体的错误原因信息
    body += "<p align=\"center\">" + message + "</p>\n";
    body += "<hr><p align=\"center\"><em>TinyWebServer (Modern C++)</em></p></body></html>";

    // 3. 极其关键：因为我们在 AddContent_ 里发现错误直接跳到了这里
    // 所以这里必须负责把剩下的头部字段 (Content-Length) 和标志着头部结束的空行 (\r\n) 补齐！
    buff.Append("Content-Length: " + std::to_string(body.size()) + "\r\n");
    buff.Append("\r\n"); // 这个空行绝不能漏掉

    // 4. 将生成的 HTML 文本塞进 Buffer 中
    buff.Append(body);
}