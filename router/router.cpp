#include "router.h"
#include <string>
#include <sys/stat.h>


void HttpRouter::Get(const std::string& path, Handler handler) {
    handlers_[MakeKey_("GET", path)] = handler;
}

void HttpRouter::Post(const std::string& path, Handler handler) {
    handlers_[MakeKey_("POST", path)] = handler;
}

void HttpRouter::Route(const HttpRequest& req, HttpResponse& res){
    std::string key = MakeKey_(req.Method(), req.Path());
    auto it = handlers_.find(key);
    if (it == handlers_.end()){
        HandleStaticResource_(req, res); // 找不到对应的业务函数，当成静态资源
    }
    else {
        it->second(req, res);
    }
}

std::string HttpRouter::MakeKey_(const std::string& method, const std::string& path) const {
    return method + " " + path;
}

void HttpRouter::HandleStaticResource_(const HttpRequest& req, HttpResponse& res){
    std::string path = req.Path();
    if (path == "/"){
        path = "/log.html";
    }
    else if (path == "/0") { 
        path = "/register.html";
    }
    else if (path == "/1") {
        path = "/log.html";
    }
    res.Init(res.SrcDir(), path, req.IsKeepAlive(), 200);
}