#include <iostream>
#include <stdio.h>
#include <stdlib.h>
//#include <libgen.h>
#include <string.h>
#include <sys/types.h>
#include <cassert>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include "thread_pool/locker.h"
#include "thread_pool/threadPool.h"
#include "http_connect/http_conn.h"

const int MAX_FD = 65535; //最大文件描述符个数
const int MAX_EVENT_NUMBER = 100000; //最大事件个数


/* 感觉是不是不用extern应该也行，但此处使用extern是为了强调该两个函数来自于另外文件 */

extern void addfd(int epollfd,int fd,bool oneShot);
extern void removefd(int epollfd,int fd);
extern void modfd(int epollfd,int fd,int ev);
//添加信号捕捉
void addsig(int sig,void(handler)(int)){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    //将sa的临时阻塞信号集设置为都阻塞的
    sigfillset(&sa.sa_mask);
    //注册信号到信号捕捉体中，后者会在接收到信号后，搭建起临时阻塞集并执行处理函数
    sigaction(sig,&sa,NULL);
}

int main(int argc,char* argv[]){
    if(argc <= 2){
        printf("按照如下格式运行：%s ip_address port_number \n",basename(argv[0]));
        exit(-1);
    }
    //获取ip
    const char* ip = argv[1];
    //获取端口号
    int port = atoi(argv[2]);
    //对SIGPIPE信号做处理，实际处理是忽略
    addsig(SIGPIPE,SIG_IGN);
    //创建线程池，并初始化线程池
    threadPool<http_conn>* pool = NULL;
    try{
        pool = new threadPool<http_conn>;
    }catch(...){
        exit(-1);
    }
    //创建一个客户连接数组用于保存所有的客户端信息
    http_conn* users = new http_conn[MAX_FD];
    assert(users);
    //创建TCP套接字
    int listenfd = socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd != -1);
    /* TODO:不懂为什么这里要端口复用 */
    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    //将socket绑定到本机ip和指定端口上，当然本机ip依赖外部指定
    int ret = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr.s_addr);
    address.sin_port = htons(port);
    ret = bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    assert(ret >= 0);
    //监听
    ret = listen(listenfd,5);
    assert(ret >= 0);
    //用于接收的epoll事件数组
    struct epoll_event epollEvents[MAX_EVENT_NUMBER];

    //创建epoll对象
    int epollfd = epoll_create(1);
    assert(epollfd != -1);
    //先将服务器监听socket放入epoll监听队列中
    addfd(epollfd,listenfd,false);
    /*
    其次，对于每个连接到服务器的客户端连接，我们也应该将其放入到监听队列中
    */
   //TODO:此处，代码唐突将epollfd送入到客户端连接http_conn对象中绑定，之前没见过，为什么这么做
   http_conn::m_epollfd = epollfd;
   //到头来，还是使用死循环来保持主线程轮询进行
   while(true){
        //-1代表永久阻塞
        int numOfReadyEvents = epoll_wait(epollfd,&epollEvents,MAX_EVENT_NUMBER,-1);
        //对于不是由于中断导致的epoll_wait返回值小于0的情况，说明epoll失败，直接退出循环
        if((numOfReadyEvents < 0 && (errno != EINTR))){
            std::cout << "--epoll failure\n";
            break;
        }
        //循环遍历就绪事件数组
        for(int i = 0;i < numOfReadyEvents;++i){
            int sockfd = epollEvents[i].data.fd;
            if(sockfd == listenfd){
                //表示服务器监听到新内容，即有客户端连接
                struct sockaddr_in clientAddress;
                socklen_t clientAddressLen = sizeof(clientAddress);
                int cfd = accept(listenfd,(struct sockaddr*)&clientAddress,&clientAddressLen);
                if(http_conn::m_user_count >= MAX_FD){
                    //目前连接数满了
                    //TODO:给客户端发送一个信息，标识服务器内部正忙
                    close(cfd);
                    continue;
                }
                //将新的客户数据，借由连接对象的初始化方法载入到数组中某一个对象中，此处直接使用客户端文件描述符作为数组索引
                users[cfd].init(cfd,&clientAddress);
            }
        }
   }

    return 0;
}