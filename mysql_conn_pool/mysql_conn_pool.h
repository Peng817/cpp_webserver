#ifndef MYSQL_CONNECTION_POOL_
#define MYSQL_CONNECTION_POOL_
#include <mysql/mysql.h>
#include <string>
#include <list>

#include "../thread_pool/locker.h"
#include "../log/log.h"

using std::string;
class connection_pool
{
public:
    connection_pool(){};
    ~connection_pool();
    void init(unsigned int max_size, string url, int port,
              string user, string pwd, string database_name);
    // 从数据库连接池中请求一个可用连接
    MYSQL *get_connection();
    // 释放一个可用连接，归还到池中
    bool release_connection(MYSQL *conn);
    // 与init相对，销毁数据库连接池
    void destroy();

private:
    locker m_lock;                  // 保护连接池的互斥访问量
    sem m_resourse;                 // 连接池资源信号量
    std::list<MYSQL *> m_conn_pool; // 连接池

    unsigned int m_max_size; // 最大连接数

    string m_url;           // mysql服务器地址
    int m_port;             // mysql服务器端口
    string m_user_name;     // 连接用户
    string m_pass_word;     // 连接密码
    string m_database_name; // 连接数据库名
};

/*
    基于RALL实现数据库连接池的自动回收机制
    关键字: C++, RAII原理、应用与实践——应该使用对象来管理资源

*/
class connection_pool_wrapper
{
public:
    connection_pool_wrapper(connection_pool &pool);
    ~connection_pool_wrapper();
    MYSQL *get_raw_connection();

private:
    connection_pool &m_pool;
    MYSQL *m_connection;
};
#endif