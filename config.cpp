#include "config.h"
#include <unistd.h> // getopt 需要
#include <cstdlib>  // atoi 需要

Config::Config() {
    // 基础配置
    PORT = 9006;
    timeoutMS = 3000;
    OPT_LINGER = 1;
    
    // 触发组合模式, 默认 0 (LT + LT), 推荐传 3 (ET + ET)
    TRIGMode = 0;

    // 资源池配置
    sql_num = 8;
    sqlPort = 3306;
    thread_num = 8;

    // 日志配置 (默认开启日志)
    close_log = 0; 
}

void Config::parse_arg(int argc, char* argv[]) {
    int opt;
    // 删除了 a (actor_model)，只保留需要的参数
    const char *str = "p:m:o:s:t:c:"; 
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
            case 'p': PORT = atoi(optarg); break;
            case 'm': TRIGMode = atoi(optarg); break;
            case 'o': OPT_LINGER = atoi(optarg); break;
            case 's': sql_num = atoi(optarg); break;
            case 't': thread_num = atoi(optarg); break;
            case 'c': close_log = atoi(optarg); break;
            default: break;
        }
    }
}