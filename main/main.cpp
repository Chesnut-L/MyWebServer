#include <cstdio>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "../locker/locker.h"
#include "../locker/locker.cpp"
#include "../threadpool/threadpool.h"
#include "../threadpool/threadpool.cpp"
#include "../http/http_conn.h"
#include "../http/http_conn.cpp"


#define MAX_FD 65536    // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 一次监听的最大的事件数量

// 添加信号捕捉
void addsig(int sig, void(handler)(int)){
    // sigaction 信号到达时处理的函数
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);

// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);

// 修改epoll中文件描述符
extern void modfd(int epollfd, int fd, int ev); 

int main(int argc, char* argv[]) 
{
    if (argc <= 1) {
        printf("按照如下格式运行： %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    // 获取端口号  字符串转为整数 
    int port = atoi(argv[1]);

    // 前置准备 若通信过程中一端断开连接，另一端仍然在输入数据，可能会产生SIGPIE信号
    // 处理SIGPIE信号 忽略该信号
    addsig(SIGPIPE, SIG_IGN);

    // 初始化线程池 
    threadpool<http_conn> * pool = NULL;
    try {
        // printf("create threadpool: \n");
        pool = new threadpool<http_conn>;
    } catch(...) {
        exit(-1);
    }
    
    // 创建数组 保存所有客户端数据信息
    http_conn * users = new http_conn[MAX_FD];

    // 网络通信 - 服务端
    // 1)创建监听socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    // 2)设置端口复用（绑定之前设置）
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // 3)绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    // 4)监听
    listen(listenfd, 5);

    // 创建epoll对象 事件数组 添加监听文件描述符
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 将监听文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true) {
        // 检测到num个事件
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((num < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        } 

        // 循环遍历事件数组
        for(int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            // 有客户端连接
            if (sockfd == listenfd){
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);

                if (http_conn::m_user_count >= MAX_FD) {
                    // 说明目前连接数已满， 给客户端提示 (不能直接回写，浏览器无法解析)
                    close(connfd);
                    continue;
                }

                // 将客户数据初始化放入数组
                users[connfd].init(connfd, client_address);

            } 
            // 判断是否是异常断开 对方异常断开或错误等事件
            else if (events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                users[sockfd].close_conn();
            }
            // 判断是否有读事件发生
            else if (events[i].events & EPOLLIN) {
                // printf("read , i : %d\n",i);
                // 一次性读取所有数据
                if (users[sockfd].read()) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            }
            // 判断是否有写事件发生 & 按位与
            else if (events[i].events & EPOLLOUT) {
                // printf("write , i : %d\n",i);
                // 一次性写完所有数据
                if (!users[sockfd].write()){
                    users[sockfd].close_conn();
                }
            }
        }
    }
    
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}