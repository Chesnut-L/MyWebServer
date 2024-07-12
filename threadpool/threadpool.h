#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

// 线程池类
template <typename T>
class threadpool 
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    //向请求队列中插入任务请求
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /* worker 工作线程运行的函数，它不断从工作队列中取出任务并执行*/
    /*  在 thread_create() 中传递函数指针时，如果该函数是一个成员函数，它的类型和参数列表会与标准的函数指针不匹配。
            因为成员函数有一个隐藏的参数，即指向类实例的指针（通常称为 this 指针）。
            为了解决这个问题，通常将成员函数声明为 static，这样它就不再依赖于任何特定的类实例，因此不需要隐藏的 this 指针参数。
            这样就可以将该函数指针传递给标准的函数指针参数。
    */ 
    static void *worker(void *arg);
    // /工作线程从请求队列中取出某个任务进行处理，注意线程同步
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};

#endif
