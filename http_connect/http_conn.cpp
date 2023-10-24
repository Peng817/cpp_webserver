#include "http_conn.h"

//向epoll中添加需要监听的文件描述符
void addfd(int epollfd,int fd,bool oneShot){
    epoll_event event;
    event.data.fd = fd;
    /* 
    内核在2.6.17之后对于客户端连接断开会触发EPOLLRDHUP和EPOLLIN
    此处将事件按此设置则我们在代码中可以处理对面挂起的情况，则不再
    需要返回到上层，即根据read、recv的返回值为0来确定客户端是否断
    开连接
     */
    event.events = EPOLLIN | EPOLLRDHUP;
    if(oneShot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
}

//从epoll中移除监听的文件描述符,并关闭
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//修改epoll中指定文件描述符的监听事件
void modfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd = fd; 
    event.events = ev | EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}