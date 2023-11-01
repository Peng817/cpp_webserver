#ifndef LOCKER_H
#define LOCKER_H
#include <pthread.h>
#include <semaphore.h>
#include <exception>


/* 互斥锁类 */
class locker{
public:
    locker();
    ~locker();
    bool lock();
    bool unlock();
private:
    pthread_mutex_t m_mutex;
};

/* 条件变量类 */
class cond{
public:
    cond();
    ~cond();
    bool wait();
    bool timewait(struct timespec* t);
    bool signal();
private:
    //TODO:此处是否可以写成一个指针，在构造的时候让外部传入,但这样容易耦合
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

/* 信号量类 */
class sem{
public:
    sem();
    sem(int sem_val,int pshared = 0);
    ~sem();
    bool wait();
    bool post();
private:
    sem_t m_sem;
};

#endif