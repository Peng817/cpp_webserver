#include <iostream>
#include <stdio.h>
#include <stdlib.h>
//#include <libgen.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include "thread_pool/locker.h"
#include "thread_pool/threadPool.h"
#include "http_connect/http_conn.h"

const int MAX_FD = 65535; //最大文件描述符个数
const int MAX_EVENT_NUMBER = 65536; //最大事件个数


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
    //创建一个数组用于保存所有的客户端信息
    http_conn* users = new http_conn[MAX_FD];
    //创建TCP套接字
    int listenfd = socket(PF_INET,SOCK_STREAM,0);
    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr.s_addr);
    address.sin_port = htons(port);
    bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    //监听
    listen(listenfd,5);
    //创建epoll对象，用于接收的epoll事件数组
    struct epoll_event epoll_events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(1);
    //将监听的文件描述符
    return 0;
}