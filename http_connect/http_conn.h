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
};

#endif