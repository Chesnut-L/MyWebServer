#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
// 信号量
class sem
{
public:
    sem() // 默认构造
    { // int sem_init(sem_t *sem, int pshared, unsigned int value);
    //该函数初始化由 sem 指向的信号对象，并给它一个初始的整数值 value。
    // pshared 控制信号量的类型，值为 0 代表该信号量用于多线程间的同步，值如果大于 0 表示可以共享，用于多个相关进程间的同步
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    sem(int num) // 构造函数 
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~sem() // 析构函数
    {
        sem_destroy(&m_sem);
    }
    bool wait() // 请求资源
    {
        return sem_wait(&m_sem) == 0; //成功返回0
    }
    bool post() // 释放资源
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

// 互斥量
class locker
{
public:
    locker()
    {       // 初始化互斥锁
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0; //成功返回0
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get() // 返回互斥量
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

//条件变量 RAII资源获取及初始化 某个共享数据达到某个值时,唤醒等待这个共享数据的线程
class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    // 等待条件变量
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    // 唤醒等待条件变量的线程
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() //以广播的方式唤醒所有等待目标条件变量的线程
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
