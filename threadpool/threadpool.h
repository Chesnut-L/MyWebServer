#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <exception>
#include <cstdio>
#include <pthread.h>
#include <list>
#include "../locker/locker.h"


// 线程池类 定义为模板类，为了代码复用 不同的任务类型都可以通用 模板参数T为任务类型
template <typename T>
class threadpool
{
public:

    threadpool(int thread_number = 8, int max_requests = 100000);
    ~threadpool();
    // 添加任务到请求队列
    bool append(T* request);

private:
    // 工作线程运行函数 不断从工作队列中取出任务并且执行
    static void* worker(void *arg);
    // 工作线程从请求队列中取出某个任务进行处理
    void run();

private:
    // 线程池中的线程数量
    int m_thread_numbers;   
    // 请求队列中最多允许的 等待处理的请求数量
    int m_max_requests;

    // 线程池数组,大小为线程数量
    pthread_t * m_threads;
    // 请求队列
    std::list<T *> m_workqueue;

    // 互斥锁 保护请求队列
    locker m_queuelocker;

    // 信号量 判断是否有任务需要处理
    sem m_queuestat;
    
    // 是否结束线程
    bool m_stop;
};


#endif