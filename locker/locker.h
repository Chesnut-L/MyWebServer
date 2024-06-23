#ifndef LOCKER_H
#define LOCKER_H


#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 线程同步机制封装类
// 互斥锁类
class locker
{
public:
    locker();   // 初始化互斥锁
    ~locker();
    bool lock();
    bool unlock();
    // 获得互斥量
    pthread_mutex_t * get();
private:
    pthread_mutex_t m_mutex;
};

// 条件变量类 
// 条件变量 RAII资源获取及初始化 某个共享数据达到某个值时,唤醒等待这个共享数据的线程
class cond{
public:
    cond();
    ~cond();
    // 等待条件变量
    bool wait(pthread_mutex_t * mutex);
    bool timewait(pthread_mutex_t * mutex, struct timespec t);
    // 唤醒等待条件变量的线程
    bool signal();
    bool broadcast();

private:
    pthread_cond_t m_cond;
};


// 信号量类
class sem{
public:
    sem();
    sem(int num);
    ~sem();
    // 请求资源
    bool wait();
    // 释放资源
    bool post();

private:
    sem_t m_sem;
};

#endif