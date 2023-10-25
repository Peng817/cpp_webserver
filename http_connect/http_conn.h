#ifndef HTTPCONN_H
#define HTTPCONN_H
#include <unistd.h>
#include <sys/epoll.h>

class http_conn{
public:
    http_conn();
    ~http_conn();
    //处理客户端请求：解析请求报文，生成响应报文
    void process();
private:
    int m_sockfd;
    //当前连接对面的地址
    struct sockaddr_in m_address;
public:
    //通过静态，使得所有客户端socket绑定到同一epollfd中
    static int m_epollfd;
    //用户连接数量
    static int m_user_count;
};

#endif