#include "threadpool.h"

// 初始化线程池
template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : 
        m_thread_numbers(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL){

    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    // 动态创建线程池数组
    m_threads = new pthread_t[m_thread_numbers];
    if (!m_threads) {
        throw std::exception();
    }
    
    // 创建thread_number个线程 并设置线程分离
    for (int i = 0; i < thread_number; i++) {
        printf("create the %dth thread\n",i+1);
        // 创建线程
        if (pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete [] m_threads;
            throw std::exception();
        }
        // 线程分离
        if (pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
    
}

template <typename T> 
threadpool<T>::~threadpool()
{
    delete m_threads;
    // 结束线程
    m_stop = true;
}

// 向请求队列中添加任务
template <typename T> 
bool threadpool<T>::append(T* request)
{
    // 加锁 操作请求队列
    m_queuelocker.lock();
    // 超出最大请求数量 解锁返回
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    // 将新任务加入请求队列
    m_workqueue.push_back(request);
    // 解锁
    m_queuelocker.unlock();
    // 释放信号量
    m_queuestat.post();
    return true;
}

// 工作线程运行的函数
template <typename T> 
void* threadpool<T>::worker(void * arg)
{
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

// 工作线程从请求队列中取出某个任务进行处理
template <typename T> 
void threadpool<T>::run()
{
    while(!m_stop) {
        // 判断有没有任务 如果没有任务 wait 一直阻塞
        m_queuestat.wait();
        // 加锁
        m_queuelocker.lock();
        // 若请求队列为空 解锁
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        // 从队列中取任务
        T * request = m_workqueue.front();
        m_workqueue.pop_front();
        // 解锁
        m_queuelocker.unlock();

        if (!request) {
            continue;
        }
        // 开始运行任务
        request->process();
    }
}