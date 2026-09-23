#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

using namespace std;

class Config
{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char*argv[]);

    //端口号
    int PORT;

    //日志写入方式 // 默认异步，暂时取消这个配置项
    //int LOGWrite;

    //触发组合模式
    int TRIGMode;

    //epoll周期
    int timeoutMS;

    //优雅关闭链接
    int OPT_LINGER;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    // 数据库端口号
    int sqlPort;

    //是否关闭日志
    int close_log;
};

#endif