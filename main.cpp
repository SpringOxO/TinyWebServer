#include "config.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "123456a";
    string databasename = "yourdb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    Log::get_instance()->init("ServerLog", config.close_log, 2000, 800000);

    // logwrite同步异步没在webserver里写，现在默认异步
    WebServer server(config.PORT, config.TRIGMode, config.timeoutMS, config.OPT_LINGER, config.thread_num, config.sql_num, config.sqlPort, user.data(), passwd.data(), databasename.data());

    LOG_INFO("========== Server started! Port: %d ==========", config.PORT);
    server.Start();

    return 0;
}