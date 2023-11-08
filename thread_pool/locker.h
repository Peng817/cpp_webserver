#ifndef LOCKER_H
#define LOCKER_H
#include <pthread.h>
#include <semaphore.h>
#include <exception>

/* 互斥锁类 */
class locker
{
public:
    locker();
    ~locker();
    bool lock();
    bool unlock();
    pthread_mutex_t *get_mutex();

private:
    pthread_mutex_t m_mutex;
};

/* 条件变量类 */
class cond
{
public:
    cond();
    ~cond();
    bool wait(pthread_mutex_t *mutex);
    bool timewait(pthread_mutex_t *mutex, struct timespec *t);
    bool signal();
    bool broadcast();

private:
    // 此处为了能让条件变量类更加自由的使用，成员变量仅仅包含pthread_cond_t本身
    // 与pthread_cond_t配套使用的互斥量pthread_mute_t需要靠外部引入
    pthread_cond_t m_cond;
};

/* 信号量类 */
class sem
{
public:
    sem();
    sem(int sem_val, int pshared = 0);
    ~sem();
    bool wait();
    bool post();

private:
    sem_t m_sem;
};

#endif