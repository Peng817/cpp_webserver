#include "mysql_conn_pool.h"

connection_pool::~connection_pool()
{
    destroy();
}

void connection_pool::init(unsigned int max_size, string url, int port, string user, string pwd, string database_name)
{
    m_url = url;
    m_port = port;
    m_user_name = user;
    m_pass_word = pwd;
    m_database_name = database_name;
    int count = 0;
    for (int i = 0; i < max_size; i++)
    {
        MYSQL *conn = NULL;
        /*
        https://dev.mysql.com/doc/c-api/8.0/en/mysql-init.html
        分配、初始化并返回一个MYSQL对象的指针
        */
        conn = mysql_init(conn);
        if (conn == NULL)
        {
            LOG_ERROR("--mysql connection pool's %dth conn failed to create", i);
        }
        /*
        试图与url指定的主机上运行的mysql服务器建立连接，反馈信息将存储在MYSQL对象上
        */
        conn = mysql_real_connect(conn, m_url.c_str(), m_user_name.c_str(),
                                  m_pass_word.c_str(), m_database_name.c_str(), port, NULL, 0);
        if (conn == NULL)
        {
            LOG_ERROR("--mysql connection pool's %dth conn failed to connect", i);
        }

        m_conn_pool.push_back(conn);
        count++;
    }
    m_resourse = sem(count); // 重定义信号量
    m_max_size = count;
    if (m_max_size < max_size)
    {
        LOG_WARN("--mysql connection pool has create %d/%d conn,and failed %d", m_max_size, max_size, max_size - m_max_size);
        printf("--mysql connection pool has create %d/%d conn,and failed %d\n", m_max_size, max_size, max_size - m_max_size);
    }
    else
    {
        LOG_INFO("--mysql conneciton pool has create %d connection.", m_max_size);
        printf("--mysql conneciton pool has create %d connection.\n", m_max_size);
    }
}

MYSQL *connection_pool::get_connection()
{
    MYSQL *conn = NULL;
    if (0 == m_conn_pool.size())
        return NULL;
    m_resourse.wait();
    m_lock.lock();
    conn = m_conn_pool.front();
    m_conn_pool.pop_front();
    m_lock.unlock();
    return conn;
}

bool connection_pool::release_connection(MYSQL *conn)
{
    if (NULL == conn)
        return false;
    m_lock.lock();
    m_conn_pool.push_back(conn);
    m_lock.unlock();
    m_resourse.post();
    return true;
}

void connection_pool::destroy()
{
    m_lock.lock();
    if (m_conn_pool.size() > 0)
    {
        for (auto it : m_conn_pool)
        {
            mysql_close(it);
        }
        m_conn_pool.clear();
    }
    m_lock.unlock();
}

connection_pool_wrapper::connection_pool_wrapper(connection_pool &pool)
    : m_pool(pool), m_connection(m_pool.get_connection())
{
}

connection_pool_wrapper::~connection_pool_wrapper()
{
    m_pool.release_connection(m_connection);
}

MYSQL *connection_pool_wrapper::get_raw_connection()
{
    return m_connection;
}
