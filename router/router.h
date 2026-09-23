#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <string>
#include <unordered_map>
#include <functional>

#include "../http/http_request.h"
#include "../http/http_response.h"

class HttpRouter {
public:
    // ----- 核心定义：路由处理函数的签名 -----
    // 任何业务逻辑处理函数，都必须接收解析好的 req，并去操作 res
    using Handler = std::function<void(const HttpRequest& req, HttpResponse& res)>;

    HttpRouter() = default;
    ~HttpRouter() = default;

    // ----- 现代 Web 框架风格的路由注册接口 -----
    // 用于在服务器启动时，挂载业务逻辑 (比如处理登录、注册)
    void Get(const std::string& path, Handler handler);
    void Post(const std::string& path, Handler handler);

    // ----- 核心引擎：请求派发 -----
    // HttpConn 在解析完报文后，会调用这个函数
    void Route(const HttpRequest& req, HttpResponse& res);

private:
    // ----- 核心存储字典 -----
    // Key 为 "Method Path" (例如 "POST /login")，Value 为对应的业务逻辑函数
    std::unordered_map<std::string, Handler> handlers_;

    // ----- 内部辅助逻辑 -----
    // 默认的静态资源处理逻辑 (当路径没有在 handlers_ 中注册时调用)
    void HandleStaticResource_(const HttpRequest& req, HttpResponse& res);
    
    // 生成哈希表 Key 的辅助函数
    std::string MakeKey_(const std::string& method, const std::string& path) const;
};

#endif