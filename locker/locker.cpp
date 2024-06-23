#include "locker.h"

locker::locker()
{
    // 初始化互斥锁
    // int pthread_mutex_init(pthread_mutex_t *restrict mutex, const pthread_mutexattr_t *restrict attr);
    if (pthread_mutex_init(&m_mutex, NULL) != 0) {
        throw std::exception();
    }
}

locker::~locker()
{
    pthread_mutex_destroy(&m_mutex);
}

bool locker::lock()
{
    return pthread_mutex_lock(&m_mutex) == 0;
}

bool locker::unlock()
{
    return pthread_mutex_unlock(&m_mutex) == 0;
}

pthread_mutex_t * locker::get()
{
    return &m_mutex;
}


cond::cond()
{
    // 初始化条件变量 
    // int pthread_cond_init(pthread_cond_t *restrict cond, const pthread_condattr_t *restrict attr);
    if (pthread_cond_init(&m_cond, NULL) != 0) {
        throw std::exception();
    }
}

cond::~cond()
{
    pthread_cond_destroy(&m_cond);
}

bool cond::wait(pthread_mutex_t * mutex)
{
    // 一直等待，调用了该函数，线程会阻塞。
    // int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);
    return pthread_cond_wait(&m_cond, mutex) == 0;
}

bool cond::timewait(pthread_mutex_t * mutex, struct timespec t)
{
    // 等待多长时间，调用了这个函数，线程会阻塞，直到指定的时间结束。
    // int pthread_cond_timedwait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex, const struct timespec *restrict abstime);
    return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
}

bool cond::signal()
{
    // 唤醒一个或者多个等待的线程
    return pthread_cond_signal(&m_cond) == 0;
}

bool cond::broadcast()
{
    // 唤醒所有的等待的线程
    return pthread_cond_broadcast(&m_cond) == 0;
}

sem::sem()
{
    // int sem_init(sem_t *sem, int pshared, unsigned int value);
    // pshared : 0 用在线程间 ，非 0 用在进程间
    if (sem_init(&m_sem, 0, 0) != 0) {
        throw std::exception();
    }
}

sem::sem(int num)
{
    if (sem_init(&m_sem, 0, num) != 0) {
        throw std::exception();
    }
}

sem::~sem()
{
    sem_destroy(&m_sem);
}

bool  sem::wait() {
    // 对信号量加锁，调用一次对信号量的值-1，如果值为0，就阻塞
    return sem_wait(&m_sem) == 0;
}

bool  sem::post() {
    // 对信号量解锁，调用一次对信号量的值+1
    return sem_post(&m_sem) == 0;
}