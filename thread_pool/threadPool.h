#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <pthread.h>
#include <list>
#include "locker.h"

/*
    线程池类
 */
template<typename T>
class threadPool{
public:
    threadPool(int thread_number = 8,int max_request = 10000);
    ~threadPool();
    bool append(T* request);
private:
    /*
    游双书P303："值得一提的是，在C++程序中使用pthread_creat时，该
    函数中第三个参数必须指向一个静态函数
    */
    static void* worker(void* arg);
    void run();
private:
    //线程池中线程的个数
    int m_thread_number;
    
    //线程池数组
    pthread_t* m_threads;
    
    //请求队列中允许的最大请求数
    int m_max_request;
    
    //请求队列,请求的实体类等待模板T进行实例化
    /* 为什么使用list作为队列的实现？难道需要大量的中途插入操作吗? */
    std::list<T*> m_workQueue;
    
    //线程池的互斥锁
    /* 使用互斥锁来使得对线程池的访问是安全的 */
    locker m_queueLocker;
    
    //信号量
    /* 用来判定是否有任务需要处理 */
    sem m_queueStat;
    
    //是否结束线程
    bool m_stop;

};

#endif