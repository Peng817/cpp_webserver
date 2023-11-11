#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H
#include <stdlib.h> // exit
#include "../thread_pool/locker.h"

template <class T>
class block_queue
{
public:
    block_queue(int max_size = 1000);
    ~block_queue();
    // 清空队列
    void clear();
    bool full();
    bool empty();
    bool pop_front(T &value);
    bool pop_back(T &value);
    int get_size();
    int get_max_size();
    /*
    往队列尾添加元素，最终将所有等待队列元素的线程唤醒
    生产者角色
    */
    bool push(const T &item);
    /*
    从队列头拿取元素，无元素则将阻塞等待
    */
    bool pop(T &item);
    // 发出唤醒信号，请求清理一下队列
    void flush();
    // 关闭队列，队列不再能拿出东西，所有阻塞进程都不再阻塞返回false
    void close();

private:
    locker m_mutex;
    cond m_cond;
    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
    bool m_is_close;
};

template <class T>
inline block_queue<T>::block_queue(int max_size) : m_mutex(locker()), m_cond(cond()), m_array(NULL),
                                                   m_size(0), m_max_size(max_size), m_front(-1), m_back(-1)
{
    if (max_size <= 0)
    {
        exit(-1);
    }
    // 在构造时new[]一片空间存放T的数组，析构时将会delete[]
    m_array = new T[max_size];
    m_is_close = false;
}

template <class T>
inline block_queue<T>::~block_queue()
{
    close();
    // 上互斥锁确保析构函数是原子操作
    m_mutex.lock();
    if (m_array != NULL)
        delete[] m_array;
    m_mutex.unlock();
}

template <class T>
inline void block_queue<T>::clear()
{
    // 上互斥锁确保清空数组的操作是原子化
    m_mutex.lock();
    m_size = 0;
    m_front = -1;
    m_back = -1;
    m_mutex.unlock();
}

template <class T>
inline bool block_queue<T>::full()
{
    // 上互斥锁确保判满的操作是原子化，但感觉存在潜在风险
    m_mutex.lock();
    if (m_size >= m_max_size)
    {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template <class T>
inline bool block_queue<T>::empty()
{
    m_mutex.lock();
    if (0 >= m_size)
    {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template <class T>
inline bool block_queue<T>::pop_front(T &value)
{
    /*
    尝试以更符合规范的方式想实现 T& front(),结果发现，这样的实现模式需要在
    front()调用时如果没有头元素，抛出异常从而要在上层进行异常捕获更麻烦；所以
    还是没改
    */
    // 原子化操作读数组头值，如果此处实现的互斥锁是读写锁，则效率会更好
    m_mutex.lock();
    if (0 == m_size)
    {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_front];
    m_mutex.unlock();
    return true;
}

template <class T>
inline bool block_queue<T>::pop_back(T &value)
{
    // 原子化操作读数组尾值
    m_mutex.lock();
    if (0 == m_size)
    {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_back];
    m_mutex.unlock();
    return true;
}

template <class T>
inline int block_queue<T>::get_size()
{
    // 此处需要额外申请变量，纯粹是因为无法return临界区的值
    int tmp = 0;
    m_mutex.lock();
    tmp = m_size;
    m_mutex.unlock();
    return tmp;
}

template <class T>
inline int block_queue<T>::get_max_size()
{
    int tmp = 0;
    m_mutex.lock();
    tmp = m_max_size;
    m_mutex.unlock();
    return tmp;
}

template <class T>
inline bool block_queue<T>::push(const T &item)
{
    m_mutex.lock();
    if (m_size >= m_max_size)
    {
        // 如果队列元素已满,则唤醒所有阻塞线程
        m_mutex.unlock();
        m_cond.broadcast();
        return false;
    }
    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item; // 赋值函数，每次入队都会发生一次拷贝
    m_size++;
    m_mutex.unlock();
    m_cond.signal();
    return false;
}

template <class T>
inline bool block_queue<T>::pop(T &item)
{
    m_mutex.lock();
    while (m_size <= 0)
    {
        if (m_is_close)
        {
            m_mutex.unlock();
            return false;
        }
        // 一般情形下，如果没有元素可以Pop,将会阻塞，直到被唤醒跳出循环
        // 如果wait失败，这说明wait行为本身出现了问题，比较罕见，此时pop结束，并返回false
        if (!m_cond.wait(m_mutex.get_mutex()))
        {
            m_mutex.unlock();
            return false;
        }
    }
    /* m_front,m_size都属于临界资源，读写时都需在上锁的状态下进行 */
    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front]; // 赋值，每次数据出队也会发生一次拷贝
    m_size--;
    m_mutex.unlock();
    return true;
}

template <class T>
inline void block_queue<T>::flush()
{
    m_cond.signal();
}

template <class T>
inline void block_queue<T>::close()
{
    m_mutex.lock();
    m_is_close = true;
    m_mutex.unlock();
    m_cond.broadcast();
}

#endif