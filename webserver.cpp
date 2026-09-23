#include "webserver.h"
#include "epoller/epoller.h"
#include "http/http_conn.h"
#include "log/log.h"
#include "threadpool/threadpool.h"
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>

WebServer::WebServer(
    int port, int trigMode, int timeoutMS, int optLinger,
    int threadNum, int sqlNum, int sqlPort, const char* sqlUser, 
    const char* sqlPwd, const char* dbName
){
    port_ = port;
    timeoutMS_ = timeoutMS;
    optLinger_ = optLinger;

    // 给httpconn设置全局的网站根目录路径
    char serverPath[256] = {0};
    getcwd(serverPath, 256);
    std::string rootDir = std::string(serverPath) + "/root";
    HttpConn::srcDir = rootDir;

    // 初始化数据库连接池
    connection_pool::GetInstance()->init(
        "localhost", // 默认本地数据库，如果需要可提到参数里
        sqlUser, 
        sqlPwd, 
        dbName, 
        sqlPort, 
        sqlNum       // 创建指定数量的 MySQL 连接
    );

    epoller_ = std::make_unique<Epoller>(); 
    threadpool_ = std::make_unique<ThreadPool>(threadNum); // 默认配置是8个工作线程

    InitSocket_();
    InitEventMode_(trigMode);
    InitRouter_();

    isClose_ = false;
}

WebServer::~WebServer(){
    isClose_ = true;
    close(listenFd_); // 关闭监听大门
    
    // 释放数据库连接池中的所有 MySQL 真实连接
    connection_pool::GetInstance()->DestroyPool();
}

void WebServer::Start(){
    while (!isClose_){
        int eventCnt = epoller_->Wait( timeoutMS_);
        if (eventCnt < 0 && errno != EINTR) { LOG_ERROR("Epoll Error!"); break; }
        if (eventCnt > 0){
            for (size_t i = 0; i < eventCnt; i++){
                int fd = epoller_->GetEventFd(i);
                auto events = epoller_->GetEvents(i);
                
                // 如果是新用户
                if (fd == listenFd_) {
                    HandleListen_(); 
                } 
                // 如果是异常事件，关闭连接
                else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    CloseConn_(&users_[fd]);
                } 
                // 如果是已连接的客户发来了数据 (读事件)
                else if (events & EPOLLIN) {
                    HandleRead_(&users_[fd]); 
                } 
                // 如果是可以向客户端发数据了 (写事件)
                else if (events & EPOLLOUT) {
                    HandleWrite_(&users_[fd]); 
                }
            }
        }
    }
}

bool WebServer::InitSocket_() {
    // 1. 创建 TCP 监听 Socket
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("Create socket error!");
        return false;
    }

    // 2. 优雅关闭连接 (SO_LINGER)
    struct linger optLinger = {0};
    if (optLinger_ == 1) { // 优雅退出选项关
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }
    setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));

    // 3. 端口复用 (极其关键！SO_REUSEADDR)
    // 作用：防止服务器重启时出现 "Address already in use" 导致无法立即绑定端口
    int optval = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));

    // 4. 绑定 IP 和端口
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听本机所有网卡 IP
    addr.sin_port = htons(port_);             // 注意转化为网络字节序
    
    if (bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(listenFd_);
        LOG_ERROR("Bind socket error!");
        return false;
    }

    // 5. 开启监听，维护全连接队列
    // SOMAXCONN 是系统允许的最大排队数量
    if (listen(listenFd_, SOMAXCONN) < 0) { // 这里原版写死为 5，重构填入SOMAXCONN
        close(listenFd_);
        LOG_ERROR("Listen socket error!");
        return false;
    }

    // 6. 核心重构：必须将 ListenFd 设置为非阻塞 (Non-blocking)！
    // 只要使用了 Epoll 的边缘触发 (ET) 模式，Socket 必须是非阻塞的
    int flag = fcntl(listenFd_, F_GETFL, 0);
    fcntl(listenFd_, F_SETFL, flag | O_NONBLOCK);

    // 重要！把listenfd注册到epoller
    if (!epoller_->AddFd(listenFd_, listenEvent_)) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }

    LOG_INFO("Socket listening...");
    return true;
}

void WebServer::InitEventMode_(int trigMode) {
    // 基础事件：EPOLLIN (可读), EPOLLRDHUP (TCP 底层断开事件)
    listenEvent_ = EPOLLIN | EPOLLRDHUP;
    // 连接事件必须加上 EPOLLONESHOT！保证同一个 HttpConn 永远不会被两个线程同时处理！
    connEvent_ = EPOLLIN | EPOLLONESHOT | EPOLLRDHUP; 

    // 根据配置项动态添加边缘触发 (EPOLLET) 属性
    switch (trigMode) {
        case 0: // LT + LT
            break;
        case 1: // LT + ET
            connEvent_ |= EPOLLET;
            break;
        case 2: // ET + LT
            listenEvent_ |= EPOLLET;
            break;
        case 3: // ET + ET
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
        default: // 默认推荐 ET + ET 极致性能
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
    }

    // 将最终确定的 ET 模式同步给 HttpConn 的静态变量，
    // 这样 HttpConn 内部的 Read/Write 就能知道要不要开启 do-while 循环了
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::InitRouter_() {
    // // 示例 1：处理用户的登录请求 (POST)
    // router_.Post("/api/login", [](const HttpRequest& req, HttpResponse& res) {
    //     // 这里只是演示，实际可以从 SqlConnPool 拿连接去查 MySQL
    //     std::string user = req.GetPost("user"); 
    //     std::string pwd = req.GetPost("password");
        
    //     if (user == "admin" && pwd == "123456") {
    //         // 登录成功，返回自定义的纯文本/JSON，而不是静态文件
    //         res.SetContent("{\"status\":\"success\", \"msg\":\"Welcome!\"}", "application/json");
    //     } else {
    //         // 账号密码错误
    //         res.SetContent("{\"status\":\"error\", \"msg\":\"Invalid login\"}", "application/json");
    //     }
    // });

    // // 示例 2：处理注册请求 (POST)
    // router_.Post("/api/register", [](const HttpRequest& req, HttpResponse& res) {
    //     // ... 执行插入数据库等逻辑
    //     res.SetContent("{\"status\":\"success\"}", "application/json");
    // });
    
    // --- 1. 处理用户的登录请求 (POST) ---
    router_.Post("/api/login", [](const HttpRequest& req, HttpResponse& res) {
        std::string user = req.GetPost("user"); 
        std::string pwd = req.GetPost("password");
        LOG_INFO("Logining %s %s", user.data(), pwd.data());
        
        // 基础参数校验
        if (user.empty() || pwd.empty()) {
            res.SetContent("{\"status\":\"error\", \"msg\":\"Empty user or password\"}", "application/json");
            return;
        }

        // 🚨 核心并发防线：使用 RAII 机制安全获取数据库连接！
        // 只要离开这个 Lambda 函数作用域，mysqlcon 析构时就会自动将连接归还给池子
        MYSQL* sql = nullptr;
        connectionRAII mysqlcon(sql, connection_pool::GetInstance()); //

        if (!sql) {
            res.SetContent("{\"status\":\"error\", \"msg\":\"Database Busy\"}", "application/json");
            return;
        }

        // 组装 SQL 查询语句 (查询对应的密码)
        char query[256] = {0};
        snprintf(query, 256, "SELECT passwd FROM user WHERE username='%s' LIMIT 1", user.c_str());

        // 执行 SQL 语句
        if (mysql_query(sql, query)) {
            res.SetContent("{\"status\":\"error\", \"msg\":\"Database Query Failed\"}", "application/json");
            return;
        }

        // 获取查询结果集
        MYSQL_RES* resSet = mysql_store_result(sql);
        if (!resSet) {
            res.SetContent("{\"status\":\"error\", \"msg\":\"Empty Result\"}", "application/json");
            return;
        }

        bool isLoginSuccess = false;
        // 提取结果行比对密码
        if (MYSQL_ROW row = mysql_fetch_row(resSet)) {
            std::string dbPwd(row[0]); // row[0] 对应 SELECT 语句中的 passwd 字段
            if (dbPwd == pwd) {
                isLoginSuccess = true;
            }
        }
        
        // ⚠️ 极其关键：必须手动释放 MySQL 结果集，否则会导致服务器内存持续泄漏！
        mysql_free_result(resSet); 

        // 生成最终的响应
        if (isLoginSuccess) {
            res.SetContent("{\"status\":\"success\", \"msg\":\"Welcome!\"}", "application/json");
        } else {
            res.SetContent("{\"status\":\"error\", \"msg\":\"Invalid login\"}", "application/json");
        }
    });


    // --- 2. 处理用户的注册请求 (POST) ---
    router_.Post("/api/register", [](const HttpRequest& req, HttpResponse& res) {
        std::string user = req.GetPost("user"); 
        std::string pwd = req.GetPost("password");
        LOG_INFO("Registering %s %s", user.data(), pwd.data());

        if (user.empty() || pwd.empty()) {
            res.SetContent("{\"status\":\"error\", \"msg\":\"Empty user or password\"}", "application/json");
            return;
        }

        // 同样的 RAII 手法获取连接
        MYSQL* sql = nullptr;
        connectionRAII mysqlcon(sql, connection_pool::GetInstance()); //[cite: 3, 4]

        if (!sql) {
            res.SetContent("{\"status\":\"error\", \"msg\":\"Database Busy\"}", "application/json");
            return;
        }

        // 组装 INSERT 语句
        char query[256] = {0};
        snprintf(query, 256, "INSERT INTO user(username, passwd) VALUES('%s', '%s')", user.c_str(), pwd.c_str());

        // 执行插入操作
        if (mysql_query(sql, query)) {
            // 如果插入失败，通常是因为违反了 username 的 UNIQUE 唯一约束 (用户已存在)
            res.SetContent("{\"status\":\"error\", \"msg\":\"User already exists\"}", "application/json");
        } else {
            res.SetContent("{\"status\":\"success\", \"msg\":\"Register success!\"}", "application/json");
        }
    });
}

void WebServer::HandleListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        // 1. 调用底层的 accept 函数接客
        int clientFd = accept(listenFd_, (struct sockaddr*)&addr, &len);
        
        if (clientFd <= 0) {
            // 返回 <= 0，通常是因为 errno == EAGAIN，说明门外没人了，接客完毕，直接退出循环
            return; 
        }
        LOG_INFO("New client fd = %d", clientFd);

        // 2. 满载防御：判断当前在线人数是否超过了系统的极限配置
        if (HttpConn::userCount >= MAX_FD) {
            // 给用户回复一个“服务器繁忙”的报错报文 (SendError_ 可自己简单实现)
            SendError_(clientFd, "Server busy!"); 
            LOG_WARN("Clients is full!");
            return;
        }

        // 3. 核心：为新客人在 unordered_map (或者数组) 中初始化专属的 HttpConn 对象！
        // clientFd 就是系统分配给这个新用户的唯一桌号
        users_[clientFd].Init(clientFd, addr);

        // 4. 将新客人的套接字设为非阻塞 (Non-blocking)
        // 这是使用 Epoll 边缘触发的硬性规定，不加的话读写数据时会被死死卡住
        int flag = fcntl(clientFd, F_GETFL, 0);
        fcntl(clientFd, F_SETFL, flag | O_NONBLOCK);

        // 5. 极其关键：把新客人注册到 Epoll 的监听树上！
        // 这里的 connEvent_ 是我们在 InitEventMode_ 里配好的 (EPOLLIN | EPOLLONESHOT | EPOLLET)
        epoller_->AddFd(clientFd, connEvent_);

        // 可以在这里加个定时器模块 (Timer)，记录该连接的最后活跃时间，用于剔除死连接
        // AddTimer(clientFd, timeoutMS_, ...); 

    } while (listenEvent_ & EPOLLET); // 只有开启了 ET 模式，才需要一直循环到 accept 返回 -1
}

// 只用于把任务送进线程池
void WebServer::HandleRead_(HttpConn* client) {
    assert(client);

    LOG_INFO("Client %d requesting", client->GetFd());
    threadpool_->AddTask([this, client]() {
        OnRead_(client);
    });
}

void WebServer::OnRead_(HttpConn* client) {
    assert(client);

    int readErrno = 0;
    
    // 1. 榨干网卡：调用我们写好的带有 do-while(isET) 的非阻塞读函数
    ssize_t ret = client->Read(&readErrno);
    
    // 2. 客户端断开检测
    // 如果 ret <= 0，且不是 EAGAIN (网卡缓冲区被榨干)，说明客户端由于各种原因断开了连接
    if (ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client); // 释放内存，将其从 Epoll 树上摘除
        return;
    }
    LOG_INFO("--- [Debug] Read over, start parsing ---");

    if (client->Process(router_)) {
        // 处理成功，准备写response
        LOG_INFO("--- [Debug] Process success, triggering EPOLLOUT ---");
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else {
        // 没读到东西，继续等待
        LOG_INFO("--- [Debug] Process incomplete, waiting EPOLLIN ---");
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::HandleWrite_(HttpConn* client) {
    assert(client);

    LOG_INFO("Responding to client %d", client->GetFd());
    threadpool_->AddTask([this, client]() {
        OnWrite_(client);
    });
}

void WebServer::OnWrite_(HttpConn* client) {
    assert(client);

    int writeErrno = 0;
    ssize_t ret = client->Write(&writeErrno);
    
    if (ret <= 0 && writeErrno != EAGAIN){
        CloseConn_(client); // 释放内存，将其从 Epoll 树上摘除
        return;
    }

    if (client->ToWriteBytes() <= 0){
        // 写完了
        if (client->IsKeepAlive()) {
            // 长连接，等待他下一个 HTTP 请求
            client->ResetForKeepAlive();
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
        } else {
            // 短连接：客人拿完数据就走。服务器主动一脚把他踢开，回收资源！
            CloseConn_(client);
        }
    } else {
        // 继续写
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    }
}

void WebServer::SendError_(int fd, const char* info) {
    assert(fd > 0);
    
    // 1. 组装一个极其轻量级的 HTTP 503 报错报文
    // 即使是报错，也必须符合 HTTP 协议格式，否则客户端浏览器会一直转圈或报错协议异常
    std::string buff = "HTTP/1.1 503 Service Unavailable\r\n";
    buff += "Content-Type: text/plain\r\n";
    buff += "Connection: close\r\n";
    buff += "Content-Length: " + std::to_string(strlen(info)) + "\r\n";
    buff += "\r\n";
    buff += info;

    // 2. 阻塞发送给内核网卡
    send(fd, buff.data(), buff.size(), 0);
    
    // 🚨 3. 极其致命的保命操作：发送完后，必须立刻 close 归还文件描述符！
    // 因为这个 fd 根本没有进入我们的 HttpConn 体系，如果这里不关闭，
    // 服务器的 FD 就会永远泄露，直到报出 "Too many open files" 彻底宕机。
    close(fd); 
    
    LOG_WARN("Client[%d] has been kicked: %s", fd, info);
}

void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    epoller_->DelFd(client->GetFd());
    client->Close();
}