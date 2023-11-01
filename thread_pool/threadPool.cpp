// #include "threadPool.h"

// threadPool::threadPool(int thread_number,int max_request)
// :m_thread_number(thread_number),m_max_request(max_request),
// m_stop(false),m_threads(NULL)
// {
//     if(thread_number <= 0 || max_request <= 0){
//         throw std::exception();
//     }
//     //创建线程池队列
//     m_threads = new pthread_t[m_thread_number];
//     if(!m_threads){
//         throw std::exception();
//     }
//     //创建若干个线程，并将它们设置为线程脱离
//     for(int i = 0;i < thread_number;++i){
//         std::cout << "--create the " << i << "th thread...\n";
//         //创建线程，并将得到的线程tid存到线程池内
//         if(pthread_create(&m_threads[i],NULL,worker,this) != 0){
//             /*
//              并不需要关心数组内指向的多个线程本身是否会释放，其已经
//              作为脱离的线程独立于当前进程，会有系统进行自动回收，故
//              我们此处只用释放线程池数组本身占用的内存即可。
//             */
//             delete[] m_threads;
//             throw std::exception();
//         }
//         //设置为脱离线程
//         if(pthread_detach(m_threads[i])){
//             delete[] m_threads;
//             throw std::exception();
//         }
//     }
// }


// threadPool::~threadPool(){
//     delete[] m_threads;
//     m_stop = true;
// }

// bool threadPool::append(http_conn* request){
//     //给线程池上锁，确保安全添加任务队列
//     m_queueLocker.lock();
//     //如果当前任务队列的最大个数超过预设值max_request,则返回False
//     if(m_workQueue.size() > m_max_request){
//         m_queueLocker.unlock();
//         return false;
//     }
//     //正常情况下，给任务队列中添加一个任务，同时解锁线程池
//     m_workQueue.push_back(request);
//     m_queueLocker.unlock();
//     //任务队列中添加了一个任务，则给任务队列信号量添加一个信号
//     m_queueStat.post();
//     return true;
// }

// void* threadPool::worker(void* arg){
//     threadPool* obj =(threadPool*)arg;
//     obj->run();
//     return obj;
// }

// void threadPool::run(){
//     /* 
//     线程一直循环，直到m_stop参数被置为非0。线程池对象中的m_stop具有终止
//     所有自身维护的线程的能力 
//     */
//     while(!m_stop){
//         //取一个任务信号量，只有能取到任务信号量才可以做任务
//         /*
//         体现出了线程池中的线程睡眠在请求队列上，只有请求队列中有任务，进而
//         新增一个队列信号量，才会使得线程池中的一个线程醒来
//         */
//         m_queueStat.wait();
//         m_queueLocker.lock();
//         /*
//          此处迷惑：任务队列和队列信号量不是同步的吗，按理说得到了队列信号
//          就说明此时队列中有任务可做，那么此处判空的意义在于什么呢。
//         */
//         if(m_workQueue.empty()){
//             m_queueLocker.unlock();
//             continue;
//         }
//         // -- 线程池临界区 -- 
//         //任务队列出队一个任务
//         http_conn* request = m_workQueue.front();
//         m_workQueue.pop_front();
//         m_queueLocker.unlock();
//         /* 这一步和上面疑惑的一样，感觉有些多余 */
//         if(!request){
//             continue;
//         }
//         //当前线程 执行 任务对象的 任务代码
//         request->process();
//     }
// }