#ifndef HTTPCONN_H
#define HTTPCONN_H
#include <iostream>
#include <map>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdarg.h>
#include "../timer/listTimer.h"
#include "../log/log.h"
#include "../mysql_conn_pool/mysql_conn_pool.h"

class util_timer;

class http_conn
{
public:
    /* 静态成员常量必须在类内定义 */
    // 读缓冲区的大小
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // 用户请求的文件名的最大长度
    static const int FILENAME_LEN = 200;

    /* 在类内声明枚举，将使得该枚举变量的作用域被限定在类空间内，避免污染全局 */

    // HTTP请求方法，这里只支持GET
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
        CHECK_STATE_EXIT:当前分析结束
    */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT,
        CHECK_STATE_EXIT
    };

    /*
        从状态机的状态，即行的读取状态，分别表示
        LINE_OK:读取到一个完整的行
        LINE_BAD:行出错
        LINE_OPEN:行数据尚且不完整
    */
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    // http_conn(){}
    // ~http_conn(){}
    // 处理客户端请求：解析请求报文，生成响应报文,由线程池中的工作线程调用
    void process();
    // 通过传入socket描述符和客户端地址初始化连接
    void init(int sockfd, const struct sockaddr_in &addr);
    // 通过传入socket描述符和客户端地址以及定时器来初始化连接
    void init(int sockfd, const struct sockaddr_in &addr, util_timer *timer);
    // 通过传入socket描述符和客户端地址，定时器以及连接池来初始化连接
    void init(int sockfd, const struct sockaddr_in &addr, util_timer *timer, connection_pool *db_connect_pool);
    // 关闭连接
    void close_conn();
    // 非阻塞读，由主线程以proactor模式调用
    bool read();
    // 非阻塞写，由主线程以proactor模式调用
    bool write();

    // 获取socketfd
    int getSockfd();

private:
    // 初始化没有公开接口进行传值的成员变量
    void init();
    // 解析http请求
    HTTP_CODE parseRequest();
    // 构建http应答
    bool processResponse(HTTP_CODE ret);

    // 解析HTTP请求行，获得请求方法，目标URL,HTTP版本
    HTTP_CODE parseRequestLine(char *text);
    // 解析首部字段，目前只解析了请求体长度，host,是否保持连接
    HTTP_CODE parseHeaders(char *text);
    // 解析报文主体
    HTTP_CODE parseContent(char *text);
    // 检测一行
    LINE_STATUS parseLine();
    // 获取一行
    char *getLine();
    // 执行请求。
    /*
        核心逻辑：以GET为例。用户请求服务器上的某个资源，并通过url指定了资源在
        服务器上的位置，则我们要做的就是将资源放到响应报文的报文主体中
    */
    HTTP_CODE doRequest();

    bool add_status_line(int status, const char *title);
    // 向报文中添加报文头，这里只实现了实体长度，实体类型，是否持续
    bool add_headers(int content_length);
    bool add_content(const char *content);
    bool add_response(const char *format, ...);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();

    // 关闭内存映射
    void unmap();

private:
    // 当前客户端连接的socket
    int m_sockfd;
    // 当前连接客户端的地址
    struct sockaddr_in m_address;
    // 读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    // 标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置
    int m_read_idx;
    // 标识读缓冲中已经分析的客户数据的最后一个字节的下一个位置
    int m_checked_idx;
    // 当前正在解析的行的起始位置
    int m_start_line;
    // 解析结果：请求目标文件的文件名
    char *m_url;
    // 解析结果：请求连接的Http版本
    char *m_version;
    // 解析结果：请求连接的方法
    METHOD m_method;
    // 解析结果：报文目标的主机名【可用于检测是否是发给本机的？】
    char *m_host;
    // 解析结果：是否保持连接
    bool m_linger;
    // 解析结果：请求体的长度
    int m_content_length;
    // 解析结果：请求体的字符串指针
    char *m_content;
    /*
    报文解析状态变量会被解析函数内的几个子解析函数改变。为了让变量能跨函数
    的作用，此时将该状态变量作为成员变量来实现在类内空间域的全局性
    */
    // 记录当前连接的报文解析状态
    CHECK_STATE m_check_state;
    // 目标文件的完整路径 = doc_root + m_url
    char m_real_file[FILENAME_LEN];
    // 目标文件被mmap到内存中的起始位置
    char *m_file_address;
    // 目标文件状态信息
    struct stat m_file_stat;

    // 读缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 标识写缓冲中已经写入的客户数据的最后一个字节的下一个位置
    int m_write_idx;
    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    struct iovec m_iv[2];
    int m_iv_count;
    // 由于大的文件体可能不会一次写完，因此，需要记录分散写的总体数据量
    int m_bytes_have_send;
    // 记录分散写已经写的数据量
    int m_bytes_to_send;

    // 绑定的数据库连接池指针
    connection_pool *m_db_connect_pool;

public:
    /* 静态成员变量必须在类外定义，因此放在了类的实现文件中定义 */
    // 通过静态，使得所有客户端socket绑定到同一epollfd中
    static int m_epollfd;
    // 用户连接数量
    static int m_user_count;

public:
    // 用于和定时器绑定的指针，该定时器会在到时后自动销毁，因此连接类不需要管
    util_timer *m_timer;
};

#endif