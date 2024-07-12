#include "config.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root"; // LYQ
    string passwd = "debtor"; // LYQ is pig hahaha
    string databasename = "yourdb";

    //命令行解析
    Config config;
    // 接受命令行参数并赋值 参数保存在config类对象里
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化服务器 端口 用户名 密码 数据库名 日志写入方式 关闭方式 触发方式 数据库连接数量 
    // 线程数量 是否关闭日志 并发模型选择(reactor,proactor)
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    

    //日志系统初始化
    server.log_write();

    //数据库初始化和将用户名密码加载到服务器
    server.sql_pool();

    //线程池创建初始化
    server.thread_pool();

    //触发模式
    server.trig_mode(); //配置listen和connect的触发模式

    //监听 上面是一些初始化和配置 从监听开始程序开始运作 
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}