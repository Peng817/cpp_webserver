#ifndef HTTPCONN_H
#define HTTPCONN_H
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

class http_conn{
public:
    http_conn(){}
    ~http_conn(){}
    //处理客户端请求：解析请求报文，生成响应报文,由线程池中的工作线程调用
    void process();
    //通过传入socket描述符和客户端地址初始化连接
    void init(int sockfd,const struct sockaddr_in& addr);
    //关闭连接
    void close_conn();
    //非阻塞读
    bool read();
    //非阻塞写 
    bool write();
private:
    
private:
    int m_sockfd;
    //当前连接客户端的地址
    struct sockaddr_in m_address;
public:
    //通过静态，使得所有客户端socket绑定到同一epollfd中
    static int m_epollfd;
    //用户连接数量
    static int m_user_count;
};


#endif