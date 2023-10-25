#include "http_conn.h"

//设置文件描述符为非阻塞
int setNoBlocking(int fd){
    int oldFlag = fcntl(fd,F_GETFL);
    int newFlag = oldFlag | O_NONBLOCK;
    fcntl(fd,F_SETFL,newFlag);
    return oldFlag;
}

//向epoll中添加需要监听的文件描述符
void addfd(int epollfd,int fd,bool oneShot){
    epoll_event event;
    event.data.fd = fd;
    /* 
    内核在2.6.17之后对于客户端连接断开会触发EPOLLRDHUP和EPOLLIN
    此处将事件按此设置则我们在代码中可以处理对面挂起的情况，则不再
    需要返回到上层，即根据read、recv的返回值为0来确定客户端是否断
    开连接.此处代码和书上不同之处在于没有引入ET工作模式，这是因为
    由于后面引入了oneShot工作模式，故有没有ET影响不大。
     */
    event.events = EPOLLIN | EPOLLRDHUP;
    if(oneShot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    /*
    由于引入了oneshot模式，所以之后读数据必须保证一次性读完，而一次性
    读完需要文件描述符本身是非阻塞的，不然当面对没有数据可读时，不太好
    处理。
    */
    setNoBlocking(fd);
}

//从epoll中移除监听的文件描述符
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
}

//修改epoll中指定文件描述符的监听事件
void modfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd = fd; 
    event.events = ev | EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//定义类的静态成员变量
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

void http_conn::process()
{
    // 解析HTTP请求
    std::cout<<"--已经解析请求，发出响应报文\n";
    // 生成响应
}

void http_conn::init(int sockfd, const struct sockaddr_in &addr)
{
    m_sockfd = sockfd;
    //TODO:直接对结构体使用赋值构造函数?
    m_address = addr;
    //TODO:端口复用,书上说这两行端口复用是为了避免TIME_WAIT状态，仅用于调式，实际使用要去掉
    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd,m_sockfd,true);
    m_user_count++;
}

void http_conn::close_conn()
{
    //关闭连接应该做的事：将连接从epoll中移除，将连接描述符关闭,将成员变量悬置，将用户计数-1
    if(m_sockfd != -1){

        removefd(m_epollfd,m_sockfd);
        close(m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_conn::read()
{
    std::cout << "--一次性读出数据\n";
    return true;
}

bool http_conn::write()
{
    std::cout << "--一次性写出数据\n";
    return true;
}
