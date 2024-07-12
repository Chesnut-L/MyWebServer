#include "threadpool.h"

// 构造函数 初始化线程池
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{   
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception(); // 只抛出异常
    // 描述线程池的数组
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {   // int pthread_create(pthread_t *thread, const pthread_attr_t *attr,void *(*start_routine) (void *), void *arg);
 
        //返回值：成功返回0，失败返回错误编号
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        // if (pthread_create(m_threads + i, NULL, worker, NULL) != 0)
        {   // 失败释放内存 抛出异常
            delete[] m_threads;
            throw std::exception();
        }
        // 分离 主线程子线程分离 子线程结束自动回收 成功返回0
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 析构 释放
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}
template <typename T>
bool threadpool<T>::append(T *request, int state)
{   
    // 加锁  操作请求队列
    m_queuelocker.lock();
    // 超出最大任务数量 解锁 返回失败
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 信号量 释放
    return true;
}
template <typename T> // proactor下使用
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();   // 请求资源
        m_queuelocker.lock();   //加锁
        // 工作队列不为空
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock(); // 为空释放
            continue;
        }
        // 取出一个任务
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();//解锁
        if (!request)
            continue;
            // reactor
        if (1 == m_actor_model)
        {
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            // cout << proactor << endl;
            // proactor 取出一个数据库连接
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            // 处理
            request->process();
        }
    }
}