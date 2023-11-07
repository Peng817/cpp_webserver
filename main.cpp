#include <iostream>
#include <stdio.h>
#include <stdlib.h>
// #include <libgen.h>
#include <string.h>
#include <sys/types.h>
#include <cassert>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include "thread_pool/locker.h"
#include "thread_pool/threadPool.hpp"
#include "http_connect/http_conn.h"

const int MAX_FD = 65535;            // 最大文件描述符个数
const int MAX_EVENT_NUMBER = 100000; // 最大事件个数
const int TIME_SLOT = 5;             // alarm信号频率
static int pipefd[2];
/* 感觉是不是不用extern应该也行，但此处使用extern是为了强调该两个函数来自于另外文件 */

extern void addfd(int epollfd, int fd, uint32_t ev);
extern void removefd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);
extern int setNoBlocking(int fd);

// 信号处理函数
void sigHandler(int sig)
{
    int saveErrno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = saveErrno;
}

// 添加用于定时器的信号捕捉
// TODO:尚不清楚信号反应结构体多一个SA_RESTART参数会有什么影响。
// 为了确保函数正确运行，对不同场景的信号使用不同的注册机制
void addRestartSig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    // 将信号反应结构体的参数flag添加上 SA_RESTART，这将重新调用被该信号终止的信号调用
    sa.sa_flags |= SA_RESTART;
    // 将sa的临时阻塞信号集设置为都阻塞的
    sigfillset(&sa.sa_mask);
    // 注册信号到信号捕捉体中，后者会在接收到信号后，搭建起临时阻塞集并执行处理函数
    sigaction(sig, &sa, NULL);
}

// 添加信号捕捉
void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    // 将sa的临时阻塞信号集设置为都阻塞的
    sigfillset(&sa.sa_mask);
    // 注册信号到信号捕捉体中，后者会在接收到信号后，搭建起临时阻塞集并执行处理函数
    sigaction(sig, &sa, NULL);
}

// 定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之，同时将连接对象和定时器解绑
void cb_func(http_conn *user)
{
    printf("--timer call back it's client to close fd %d\n", user->getSockfd());
    user->close_conn();
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("按照如下格式运行：%s ip_address port_number \n", basename(argv[0]));
        exit(-1);
    }
    // 获取ip
    const char *ip = argv[1];
    // 获取端口号
    int port = atoi(argv[2]);
    // 对SIGPIPE信号做处理，实际处理是忽略
    addsig(SIGPIPE, SIG_IGN);
    // 创建线程池，并初始化线程池
    threadPool<http_conn> *pool = NULL;
    // 创建定时器有序链表
    sort_timer_lst *timerList = new sort_timer_lst();
    try
    {
        pool = new threadPool<http_conn>;
    }
    catch (...)
    {
        exit(-1);
    }
    // 创建一个客户连接数组用于保存所有的客户端信息
    // TODO：感觉可以改进，一开始就创建了65536个连接对象，每个对象都维护各自的读写缓存，感觉太浪费
    http_conn *users = new http_conn[MAX_FD];
    assert(users);
    // 创建TCP套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd != -1);
    /*
    将服务器的端口设置为复用是因为，当服务器异常终止结束时，则由于在TCP连接中，服务器是
    主动发起关闭连接的一方，因此需要在连接中接收到客户端返回的LAST_ASK后，还要继续等待
    2MSL时间，而这将是使得我们不能在服务器程序主动关闭后立即重新启动的原因，因为此时上一次连接
    还未立马断开。为了强制进程立即使用出于TIME_WAIT状态连接占用的端口，我们将监听的socket选项
    设置为端口重用，此时即使监听sock在主动关闭进程后还处在TIME_WAIT状态，与之绑定的socket地址
    也可以立即重用。
    */
    // 设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // 将socket绑定到本机ip和指定端口上，当然本机ip依赖外部指定
    int ret = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr.s_addr);
    address.sin_port = htons(port);
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    // 监听
    ret = listen(listenfd, 5);
    assert(ret >= 0);
    // 用于接收的epoll事件数组
    struct epoll_event epollEvents[MAX_EVENT_NUMBER];

    // 创建epoll对象
    int epollfd = epoll_create(1);
    assert(epollfd != -1);
    // 先将服务器监听socket放入epoll监听队列中
    addfd(epollfd, listenfd, EPOLLIN);

    // 再次，将信号传递的管道的出口放入到epoll监听队列中
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setNoBlocking(pipefd[1]);
    addfd(epollfd, pipefd[0], EPOLLIN | EPOLLET);

    /* 注册信号 */
    addRestartSig(SIGALRM, sigHandler);
    addRestartSig(SIGTERM, sigHandler);

    http_conn::m_epollfd = epollfd;
    bool stop_server = false;
    bool timeout = false;
    // 先初发出一个计时信号
    alarm(TIME_SLOT);
    // printf("--first alarm will to be send after %ds ... \n", TIME_SLOT);
    /*
    最后，对于每个连接到服务器的客户端连接，我们也应该将其放入到监听队列中
    */

    /*
    此处，代码需要将epollfd送入到客户端连接http_conn对象中绑定，因为对于
    http_conn即一个服务器接收到的客户端连接实体，其连接的sockfd和addr属于该对
    象的私有成员变量，为了不破坏类的封装性，只能由每个连接对象本身来执行将连接送
    入到epoll监听队列中的操作。同时，由于每个连接对象理应由同一个epoll管理，所以
    其epoll值对于所有的对象来说应该是可以共享的，那么就可以将类内设置一个静态变量
    用于承接传入的epollfd值，而使得每个连接对象都可以在送入监听队列时，进入同一个
    epoll监听队列。
    */
    while (!stop_server)
    {
        //-1代表永久阻塞
        int numOfReadyEvents = epoll_wait(epollfd, epollEvents, MAX_EVENT_NUMBER, -1);
        // 对于不是由于中断导致的epoll_wait返回值小于0的情况，说明epoll失败，直接退出循环
        if ((numOfReadyEvents < 0 && (errno != EINTR)))
        {
            std::cout << "--epoll failure\n";
            break;
        }
        // 循环遍历就绪事件数组
        for (int i = 0; i < numOfReadyEvents; ++i)
        {
            int sockfd = epollEvents[i].data.fd;
            util_timer *timer = NULL;
            if (sockfd != listenfd && sockfd != pipefd[0])
            {
                timer = users[sockfd].m_timer;
            }
            if (sockfd == listenfd)
            {
                // 表示服务器监听到新内容，即有客户端连接
                struct sockaddr_in clientAddress;
                socklen_t clientAddressLen = sizeof(clientAddress);
                int cfd = accept(listenfd, (struct sockaddr *)&clientAddress, &clientAddressLen);
                if (http_conn::m_user_count >= MAX_FD)
                {
                    // 目前连接数满了
                    // TODO:需要给客户端发送一个信息，标识服务器内部正忙
                    close(cfd);
                    continue;
                }
                /*
                将新的客户连接数据，借由连接对象的初始化方法载入到数组中某一个对象中，
                此处直接使用客户端文件描述符作为数组索引，理论上浪费了前三个位置。
                初始化方法会将指定对象的成员变量m_sockfd和地址m_addr赋值为传入的cfd
                和地址，同时，还会顺便将其放入到epoll监听队列中。
                */
                // TODO:疑惑，此处new创建的timer对象会放入到timerList中，由其List管理它的释放，是否不够合理
                util_timer *timer = new util_timer;
                timer->data = &users[cfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIME_SLOT;
                printf("--build 1 timer...\n");
                users[cfd].init(cfd, clientAddress, timer);
                timerList->add_timer(timer);
            }
            else if ((sockfd == pipefd[0]) && (epollEvents[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break; // 该break跳出的是for循环
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                        }
                        default:
                            break;
                        }
                    }
                }
            }
            else if (epollEvents[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 如果监听到的连接事件是异常事件，则关闭连接。
                /*
                TODO：比较不理解的在于为什么在前面只监听了EPOLLRDHUP和EPOLLIN事件，在这却
                需要检核错误时检测多出来的EPOLLUP和EPOLLERR事件，难道这两个不用主动声明也会
                被监听到吗
                */
                if (timer)
                {
                    timerList->del_timer(timer);
                }
                users[sockfd].close_conn();
            }
            else if (epollEvents[i].events & EPOLLIN)
            {
                if (users[sockfd].read())
                {
                    // 已经一次性把所有数据读完了
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIME_SLOT;
                        printf("--adjust timer once\n");
                        timerList->adjust_timer(timer);
                    }
                    pool->append(&users[sockfd]);
                }
                else
                {
                    /*
                    读数据失败，失败原因可能是读失败，或者对面关闭连接，或者内容超过连接
                    对象准备好的一整块缓存（1024Bytes）
                    */
                    if (timer)
                    {
                        timerList->del_timer(timer);
                    }
                    users[sockfd].close_conn();
                }
            }
            else if (epollEvents[i].events & EPOLLOUT)
            {
                if (!users[sockfd].write())
                {
                    // 一次性写完数据，如果没写成功，跳到该if逻辑内
                    if (timer)
                    {
                        timerList->del_timer(timer);
                    }
                    users[sockfd].close_conn();
                }
                else
                {
                    // 成功写完了数据
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIME_SLOT;
                        printf("--adjust timer once\n");
                        timerList->adjust_timer(timer);
                    }
                }
            }
        } // for(epollEvent)
        if (timeout)
        {
            time_t cur = time(NULL);
            char timestr[32];
            strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&cur));
            timerList->tick();
            printf("--%s: %d http-connet is linking!\n", timestr, http_conn::m_user_count);
            alarm(TIME_SLOT);
            timeout = false;
        }

    } // while()

    // epoll监听失败时，会跳出死循环，在监听失败后，为其收尾
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete timerList;
    delete pool;
    return 0;
}