#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <pthread.h>
#include <list>
#include <exception>
#include <iostream>
#include "../log/log.h"
#include "locker.h"

/*
    线程池类
 */
template <typename T>
class threadPool
{
public:
    threadPool(int thread_number = 8, int max_request = 10000);
    ~threadPool();
    bool append(T *request);

private:
    /*
    游双书P303："值得一提的是，在C++程序中使用pthread_creat时，该
    函数中第三个参数必须指向一个静态函数
    */
    static void *worker(void *arg);
    void run();

private:
    // 线程池中线程的个数
    int m_thread_number;

    // 线程池数组
    pthread_t *m_threads;

    // 请求队列中允许的最大请求数
    int m_max_request;

    // 请求队列,请求的实体类等待模板T进行实例化
    /* 为什么使用list作为队列的实现？难道需要大量的中途插入操作吗? */
    std::list<T *> m_workQueue;

    // 线程池的互斥锁
    /* 使用互斥锁来使得对线程池的访问是安全的 */
    locker m_queueLocker;

    // 信号量
    /* 用来判定是否有任务需要处理 */
    sem m_queueStat;

    // 是否结束线程
    bool m_stop;
};

template <typename T>
threadPool<T>::threadPool(int thread_number, int max_request)
    : m_thread_number(thread_number), m_max_request(max_request),
      m_stop(false), m_threads(NULL)
{
    if (thread_number <= 0 || max_request <= 0)
    {
        throw std::exception();
    }
    // 创建线程池队列
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
    {
        throw std::exception();
    }
    // 创建若干个线程，并将它们设置为线程脱离
    for (int i = 0; i < thread_number; ++i)
    {
        // 创建线程，并将得到的线程tid存到线程池内
        if (pthread_create(&m_threads[i], NULL, worker, this) != 0)
        {
            /*
                如果数组内指向的多个线程还未在脱离之前，出现线程创建失败，则
                当前进程有责任释放之前创建好的线程
            */
            delete[] m_threads;
            throw std::exception();
        }
        // 设置为脱离线程
        if (pthread_detach(m_threads[i]))
        {
            /*
                如果数组内指向的多个线程还未全部脱离之前，出现线程脱离失败，则
                当前进程有责任释放之前创建好的线程
            */
            delete[] m_threads;
            throw std::exception();
        }
        LOG_DEBUG("--thread pool create the %dth thread,pid:%ld", i, m_threads[i]);
    }
    printf("--thread pool has created %d threads...\n", m_thread_number);
    LOG_INFO("--thread pool has created %d threads...", m_thread_number);
}

template <typename T>
threadPool<T>::~threadPool()
{
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadPool<T>::append(T *request)
{
    // 给线程池上锁，确保安全添加任务队列
    m_queueLocker.lock();
    // 如果当前任务队列的最大个数超过预设值max_request,则返回False
    if (m_workQueue.size() > m_max_request)
    {
        m_queueLocker.unlock();
        return false;
    }
    // 正常情况下，给任务队列中添加一个任务，同时解锁线程池
    m_workQueue.push_back(request);
    m_queueLocker.unlock();
    // 任务队列中添加了一个任务，则给任务队列信号量添加一个信号
    m_queueStat.post();
    return true;
}

template <typename T>
void *threadPool<T>::worker(void *arg)
{
    threadPool *obj = (threadPool *)arg;
    obj->run();
    return obj;
}

template <typename T>
void threadPool<T>::run()
{
    /*
    线程一直循环，直到m_stop参数被置为非0。线程池对象中的m_stop具有终止
    所有自身维护的线程的能力
    */
    while (!m_stop)
    {
        // 取一个任务信号量，只有能取到任务信号量才可以做任务
        /*
        体现出了线程池中的线程睡眠在请求队列上，只有请求队列中有任务，进而
        新增一个队列信号量，才会使得线程池中的一个线程醒来
        */
        m_queueStat.wait();
        m_queueLocker.lock();
        /*
         此处迷惑：任务队列和队列信号量不是同步的吗，按理说得到了队列信号
         就说明此时队列中有任务可做，那么此处判空的意义在于什么呢。
        */
        if (m_workQueue.empty())
        {
            m_queueLocker.unlock();
            continue;
        }
        // -- 线程池临界区 --
        // 任务队列出队一个任务
        T *request = m_workQueue.front();
        m_workQueue.pop_front();
        m_queueLocker.unlock();
        /* 这一步和上面疑惑的一样，感觉有些多余 */
        if (!request)
        {
            continue;
        }
        // 当前线程 执行 任务对象的 任务代码
        LOG_DEBUG("--[线程池]pid=%ld 线程开始一次作业", pthread_self());
        request->process();
        LOG_DEBUG("--[线程池]pid=%ld 线程结束一次作业", pthread_self());
    }
}

#endif