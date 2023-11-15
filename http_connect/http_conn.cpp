#include "http_conn.h"

// 定义HTTP响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

// 服务器根目录
const char *doc_root = "/home/pengyan/webserver/resources";
// 设置文件描述符为非阻塞
int setNoBlocking(int fd)
{
    int oldFlag = fcntl(fd, F_GETFL);
    int newFlag = oldFlag | O_NONBLOCK;
    fcntl(fd, F_SETFL, newFlag);
    return oldFlag;
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, uint32_t target_events)
{
    epoll_event event;
    event.data.fd = fd;
    /*
    内核在2.6.17之后对于客户端连接断开会触发EPOLLRDHUP和EPOLLIN
    此处将事件按此设置则我们在代码中可以处理对面挂起的情况，则不再
    需要返回到上层，即根据read、recv的返回值为0来确定客户端是否断
    开连接.此处代码和书上不同之处在于没有引入ET工作模式，这是因为
    由于后面引入了oneShot工作模式，故有没有ET影响不大。
     */
    // 监听3个基本事件：文件描述符关闭，对端连接异常断开，发生错误
    event.events = EPOLLHUP | EPOLLRDHUP | EPOLLERR;
    // 额外监听指定事件
    event.events |= target_events;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    /*
    由于引入了oneshot模式，所以之后读数据必须保证一次性读完，而一次性
    读完需要文件描述符本身是非阻塞的，不然当面对没有数据可读时，不太好
    处理。
    */
    setNoBlocking(fd);
}

// 从epoll中移除监听的文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
}

// 修改epoll中指定文件描述符的监听事件
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/*
    使用连接池中的一个连接去查询数据库中的注册信息表
    如果当前连接池中的连接全部被占用而导致无法查询数据
    ，则不会阻塞，直接返回false。
*/
bool get_registration_map(
    connection_pool *mysql_conn_pool,
    std::map<std::string, std::string> &regis_map)
{
    regis_map.clear();
    MYSQL *mysql = NULL;
    // 使用资源控制类从资源池中抓取一个资源
    connection_pool_wrapper conn_pool_warpper((*mysql_conn_pool));
    // 取出资源控制类抓取到的资源
    mysql = conn_pool_warpper.get_raw_connection();
    if (mysql == NULL)
    {
        // 说明当前连接池没有可用连接
        return false;
    }
    // 查询和存储并不需要加锁来实现原子操作，因为连接池中每个连接都被独立占用
    if (mysql_query(mysql, "SELECT username,password FROM user"))
    {
        LOG_ERROR("mysql:SELECT error:%s\n", mysql_error(mysql));
    }
    MYSQL_RES *res_mysql = mysql_store_result(mysql);
    MYSQL_ROW row;
    string tmp_username;
    string tmp_password;
    while (row = mysql_fetch_row(res_mysql))
    {
        tmp_username = row[0];
        tmp_password = row[1];
        regis_map[tmp_username] = tmp_password;
    }
}

// 定义类的静态成员变量
/*
值得注意的是，如果将静态成员变量的值在头文件的类外进行定义，则会触发多重定义
这和之前写简单程序时，随手将静态成员变量的定义写在头文件内的习惯相悖
 */
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

void http_conn::process()
{
    // 解析HTTP请求
    HTTP_CODE parseReturn = parseRequest();
    LOG_DEBUG("--解析结果：%d [0:NOREQUEST,1:GETREQUEST]", parseReturn);
    if (parseReturn == NO_REQUEST)
    {
        /*
        当前连接传来的数据还是不够完整，将连接socket在epoll中的状态修改回读就绪
        并结束当前线程的执行，之后会在主线程的下一轮epoll检测中，再次检测到该连
        接，由于采用proactor模式，则主线程会再次将执行该对象的读函数，从而将可能的
        新内容继续搬到当前连接的读缓存内，使其有可能变为一个完整的请求报文。
        */
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    //  生成响应
    //  传入的parseReturn可能是除了noreq之外的所有状态包括bad和一些成功的格式
    bool writeReturn = processResponse(parseReturn);
    if (!writeReturn)
    {
        // 如果写失败了，则关闭连接
        close_conn();
    }
    // 写入到缓存，将连接作为任务丢入到epoll连接队列中，声明其写就绪

    modfd(m_epollfd, m_sockfd, EPOLLOUT);
    // 结束线程
}

void http_conn::init(int sockfd, const struct sockaddr_in &addr)
{
    m_sockfd = sockfd;
    // TODO:直接对结构体使用赋值函数?
    m_address = addr;
    /*
    一般来说，端口复用是为了让主动关闭的那一端在重启之后也能强制忽略上一次进程中已建立连接的还没完全断开的状态
    而强制重新利用之前未完全断开的工作连接，进而马上就能重新建立起连接，便于调试。
    */
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    /*
    ET的工作模式：对于其上注册的一次事件，每次就绪时只触发一次
    ONESHOT的工作模式：对于一个文件描述符，只最多触发一个事件，且只触发一次。
    用EPOLLSHOT的原因：
     由于TCP的IO连接的工作环境处于多线程环境下，如果不用ONESHOT，即使使用了ET的epoll
     监听模式，在一个线程监听到ET模式的EPOLLIN后，去进行处理，此时如果在处理的过程
     中，该连接又传来数据，虽然线程准备一口气全部读完，但是由于新的就绪又将触发一次
     ET模式下EPOLLIN，这将导致新的线程得到信号，也来处理该连接的读缓存，这将导致在
     多线程环境下，多个线程处理一个连接，这不是我们所期望的。所以使用EPOLLSHOT，确保
     多线程环境下，一个连接的读就绪仅会触发一次，这确保了只会有一个线程来处理该连接
     触发后，线程将会不断读，直到将连接的数据读完，则才会重新注册事件，使得连接能再次被读写

    EPOLLSHOT模式在当前项目的伪proactor模式下，貌似不起作用，因为在该模式下，
    处理连接读写的仅有主线程，而辅助的多线程并不参与到连接上的读写过程。所以按理说
    即使不采用ONESHOT模式，该项目也能正常运行，但如果采用的是REACTOR事件模式，则
    由辅助线程来自行进行数据的读写，则十分有必要添加ONESHOT监听事件模式。
    */
    addfd(m_epollfd, m_sockfd, EPOLLIN | EPOLLONESHOT | EPOLLET);
    m_user_count++;
    init();
}

void http_conn::init(int sockfd, const sockaddr_in &addr, util_timer *timer)
{
    m_timer = timer;
    init(sockfd, addr);
}

void http_conn::init(int sockfd, const sockaddr_in &addr, util_timer *timer, connection_pool *db_connect_pool)
{
    m_db_connect_pool = db_connect_pool;
    init(sockfd, addr, timer);
}

void http_conn::close_conn()
{
    // 关闭连接应该做的事：将连接从epoll中移除，将连接描述符关闭,将成员变量悬置，将用户计数-1
    // 如果m_timer有绑定定时器，则断开绑定
    if (m_sockfd != -1)
    {

        removefd(m_epollfd, m_sockfd);
        close(m_sockfd);
        m_sockfd = -1;
        // 绑定的定时器会被timerList销毁，我们只需要提前断开绑定即可
        m_timer = NULL;
        m_user_count--;
        // 绑定的数据库连接池断开
        m_db_connect_pool = NULL;
        printf("--http_conn class close connect,and pointer to timer,db_connect_pool in http_conn set to NULL.\n");
        LOG_INFO("--http_conn class close connect,and pointer to timer,db_connect_pool in http_conn set to NULL.");
        LOG_INFO("--delete 1 http_conn,now %d http-connect is linking!", http_conn::m_user_count);
        // 每次结束一个连接，则将日志文件指针维护的缓存强推到日志文件里，刷新缓存
        log::get_instance()->file_flush();
    }
}

bool http_conn::read()
{
    // 如果当前读索引超过读缓存，说明读缓存已经装不下整个数据报，此时将会关闭连接
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    // 读取到的字节
    int bytesRead = 0;
    while (1)
    {
        /*
        我们希望对于read()调用能够一次性将客户连接的所有的数据读出来，所以此处写
        一个死循环读取。同时我们又希望每轮读取得到的数据最终都会有序的放在一块内存
        上，方便之后的使用，因此我们需要一整块读缓冲空间和一个可移动的指针，指针初
        始指向缓存区头部，每读一次，我们记录实际读取到的长度，并将指针向后移动这么
        多的长度，在下一次读取，我们将recv接收的读缓冲地址设置为指针指向的位置，则
        可以让下一次数据接着已有的数据后面存放在内存缓存内。同时，我们告知recv的缓
        冲区长度就是内存缓存的剩余长度，这样我们也可以利用recv的机制做到安全读取。
        */
        bytesRead = recv(m_sockfd, m_read_buf + m_read_idx,
                         READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytesRead == -1)
        {
            /*
            errno 是 EAGAIN和EWOULDBLOCK表示当前读失败是因为非阻塞读取导致数据
            读完，这种情况下，应该退出死循环，代表读取结束，且读取成功。而其他情
            况的error代表读取失败，则应该返回读取错误。
             */
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (bytesRead == 0)
        {
            /*
            recv返回结果为0，代表客户端已经关闭连接，则本次连接的读取任务视作被
            中断，应该直接返回读取失败
            */
            return false;
        }
        else
        {
            // recv成功
            m_read_idx += bytesRead;
        }
    }
    char ip[16] = {0};
    inet_ntop(AF_INET, &m_address.sin_addr.s_addr, ip, INET_ADDRSTRLEN);
    std::cout << "--接收到请求报文...\n";
    LOG_INFO("--从%s:%d接收到请求报文如下:\n%s", ip, m_address.sin_port, m_read_buf);
    return true;
}

bool http_conn::write()
{
    // 修改参考：https://blog.csdn.net/ad838931963/article/details/118598882
    int temp = 0;
    // TODO:感觉多余
    if (m_bytes_to_send == 0)
    {
        // 将要发送的字节为0，这一次响应结束。
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        // 分散写，且一次性要求写完
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                /*
                主线程写失败了，但是是因为m_sockfd写缓存暂时没空间,于是
                让主线程退出该连接的write，且同时将该连接重新作为任务挂到队列中。
                主线程会在下一次epoll轮次中取到该连接，再次write，再次进入这个循环
                */
                return true;
            }
            // 主线程写失败了，且是因为别的原因，那么说明该连接有问题，将会关闭映射
            // 并返回false，而这会在之后使得连接关闭
            unmap();
            return false;
        }
        // 可以正常将对象的就绪写缓存 写到sockfd的TCP写缓存中，那就一直不断写
        m_bytes_to_send -= temp;
        m_bytes_have_send += temp;
        // 如果已发送的字节大于报头，证明报头发送完毕，且m_iv_count > 1
        if (m_bytes_have_send >= m_iv[0].iov_len)
        {
            // 将分散写的第一块写来源待写数据长度置0，不再需要对第一块的内容做输出
            m_iv[0].iov_len = 0;
            // 不断更新第二块写来源写的进度
            m_iv[1].iov_base = m_file_address + (m_bytes_have_send - m_write_idx);
            m_iv[1].iov_len = m_bytes_to_send;
        }
        else
        {
            // 否则，已发送的字节不如报头，说明一次发送没有将报头发送完
            m_iv[0].iov_base = m_write_buf + m_bytes_have_send;
            m_iv[0].iov_len -= temp;
        }

        // 如果将发送的数据长度小于等于0，代表已经没有数据可发，代表发送完毕
        if (m_bytes_to_send <= 0)
        {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            std::cout << "--已经发出" << m_bytes_have_send << " bytes 数据。\n";
            char ip[16] = {0};
            inet_ntop(AF_INET, &m_address.sin_addr.s_addr, ip, INET_ADDRSTRLEN);
            LOG_INFO("--已向%s:%d发出%d bytes数据", ip, m_address.sin_port, m_bytes_have_send);
            LOG_INFO("--发送响应报文头如下:\n%s", m_write_buf);
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if (m_linger)
            {
                std::cout << "--connect is keep-alive...\n";
                LOG_INFO("--connect is keep-alive...");
                // 对除了对象本身的sockfd和地址以外，连接对象其余的成员数据清空
                init();
                return true;
            }
            else
            {
                // 返回了false，之后本对象指向的连接将关闭。
                std::cout << "--connect is not keep-alive.\n";
                LOG_INFO("--connect is not keep-alive...");
                return false;
            }
        }
    }
}

int http_conn::getSockfd()
{
    return m_sockfd;
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_read_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_bytes_to_send = 0;
    m_bytes_have_send = 0;
    m_content_length = 0;
    m_content = NULL;
    m_method = GET;
    m_url = NULL;
    m_version = NULL;
    m_host = NULL;
    m_linger = false;
    bzero(m_real_file, FILENAME_LEN);
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
}

http_conn::HTTP_CODE http_conn::parseRequest()
{
    // 从状态机的起始状态是LINE_OK
    LINE_STATUS lineStatus = LINE_OK;
    // 主状态机的起始状态是CHECK_STATE_REQUESTLINE
    m_check_state = CHECK_STATE_REQUESTLINE;
    char *text = 0;
    HTTP_CODE ret;

    // 从状态机执行的更底层，而主状态机依赖从状态机返回的结果，我们使用一个循环来同时运行两台机器
    while (m_check_state != CHECK_STATE_EXIT && lineStatus == LINE_OK)
    {
        lineStatus = parseLine();
        // 如果从状态机发生状态改变，则当前主状态机依赖的状态不再，此时主状态机不再工作
        // 确保主状态机的运行是在指定从状态机状态下进行
        if (lineStatus != LINE_OK)
            continue;
        /* process in LINE_OK */
        // 获取行数据首地址
        text = getLine();
        // 解析正确时，解析指针在另一个角度可看成是下一行首地址
        m_start_line = m_checked_idx;
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parseRequestLine(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            // 如果安全解析，则继续下一轮循环 解析下一句
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parseHeaders(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                m_check_state = CHECK_STATE_EXIT;
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            /*
            对于请求体，其解析方式不再像请求头请求行一样逐行解析，而是从text开始后面
            的整体内容直接解析，这使得要么整体解析成功返回GETREQ,要么解析失败返回BAD
            而如果在解析开始时检测到内容量+check指针(此时check指针指向请求头+空行之后
            一个位置，即请求体头的地方)超过了读指针，这代表请求体内容还没读完，返回
            NO_REQ,而这在上面的逻辑中不会返回，即执行到下面代码，因此，我们将lineSate
            置为OPEN,这样它将使得从状态机切换状态，而我们为OPEN状态准备了NOREQ信号，
            这使得调用该函数的process()函数将连接重新作为读就绪放入epoll，使得该连接
            维护的读缓存可以有机会继续读入新的内容。
            */
            ret = parseContent(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                m_check_state = CHECK_STATE_EXIT;
                break;
            }
            lineStatus = LINE_OPEN;
            break;
        }
        default:
        {
            return INTERNAL_ERROR; // 内部错误
        }
        } // switch
    }

    /* lineStatus = BAD / OPEN || check_state = EXIT*/
    // 处理从状态机的两个出口状态
    if (lineStatus == LINE_BAD)
    {
        return BAD_REQUEST;
    }
    else if (lineStatus == LINE_OPEN)
    {
        return NO_REQUEST;
    }

    /* check_state = EXIT */
    // 如果不是从状态机到达出口状态，则处理主状态机的出口状态
    return doRequest();
}

bool http_conn::processResponse(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    case BAD_REQUEST:
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
        {
            return false;
        }
        break;
    case NO_RESOURCE:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    case FORBIDDEN_REQUEST:
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    case FILE_REQUEST:
        add_status_line(200, ok_200_title);
        add_headers(m_file_stat.st_size);
        /*
        我们不再将已经通过mmap映射进来的报文主体写入到写缓存进行整合
        这毫无疑问会使用额外的内存，会浪费额外的时间。
        我们采取一种直接将连接对象内维护的两块分开的写内存一起写的方式writev
        完成从连接的两块写缓存转移到socket在系统中维护的写缓存过程。从而
        实现了报文在sockfd上的写就绪。
        m_iv[0]标识写缓存，其搭载了响应状态行和响应首部，m_iv[1]标识目标资源的缓存，
        其将作为响应报文的实体
        */
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv[1].iov_base = m_file_address;
        m_iv[1].iov_len = m_file_stat.st_size;
        m_bytes_to_send = m_write_idx + m_file_stat.st_size;
        m_iv_count = 2;
        return true;
    default:
        return false;
    }
    // 当发送的是错误提示的响应报文时，只需要有响应报文头
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_bytes_to_send = m_write_idx;
    m_iv_count = 1;
    return true;
}

http_conn::HTTP_CODE http_conn::parseRequestLine(char *text)
{
    // 例子 ： GET / HTTP/1.1
    /*
    课里讲可以使用正则表达式检索，效率应该会高一些，不过没学，书上实现
    也是直接拆字符串的方法，所以不折腾了。
    */

    /*
    strpbrk:在参数1字符串中匹配参数2中字符串形式的字符列表，返回参数1
    字符串中第一个出现在字符列表中的字符。
    用在此处即是找text中第一个出现空格或者'\t'的位置

    */
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        // 若返回结果为空，则说明字符串不合语法
        return BAD_REQUEST;
    }
    *m_url++ = '\0';     // 将第一个空格处变为'\0',并将m_url指向空格后一位地址
    char *method = text; // 此时text作为的字符串就是一行第一个空格前的内容
    // strcasecmp:C语言中判断字符串是否相等的函数，忽略大小写
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
    }
    else
    {
        return BAD_REQUEST;
    }
    // strspn:检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_url += strspn(m_url, " \t"); // 跳过多余空格
    // 先暂时跳过url，查找一下Http版本的首地址
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    // 截出m_url的字符串
    *m_version++ = '\0';
    // 比较m_version字符串是否符合"HTTP/1.1"，不是的话返回BAD
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    // 对于带http头的特殊处理，示例：http://192.168.56.101:9999/index.html
    if (strncasecmp(m_url, "http://", 7) == 0)
    {

        m_url += 7; // 192.168.56.101:9999/index.html
        // strchr() 用于查找字符串中的一个字符，并返回该字符在字符串中第一次出现的位置
        m_url = strchr(m_url, '/'); // /index.html
    }
    // 一般性处理
    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    // 之后m_url中字符串即是 /index.html
    // 请求行解析成功,只有一条请求行，所以请求行解析完可以直接将主状态改为head
    m_check_state = CHECK_STATE_HEADER;
    // 返回什么意义不大，只要不返回BAD就行
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parseHeaders(char *text)
{
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0')
    {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        // 请求体长度的信息可以由请求头的Content-Length字段后的值得知
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        // printf("oop! unknow header: %s\n", text);
    }
    // 意义不大，正常解析完成执行到这随便返回的一个无关紧要的值
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parseContent(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        // 在POST请求中，表单将追加在请求主体中
        // 因为请求报文已经全部放在内存缓存中，所以
        // 只需要使用一个字符串指针指向报文主体的位置即可
        m_content = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::LINE_STATUS http_conn::parseLine()
{
    // 如果状态为检测请求体，则该函数不适用，直接返回OK,交给parse_content函数处理
    if (m_check_state == CHECK_STATE_CONTENT)
    {
        return LINE_OK;
    }
    char temp;
    /* 循环很可能并不会走到m_read_idx，因为只会检查一行*/
    for (; m_checked_idx < m_read_idx; m_checked_idx++)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
            {
                // 说明不完整，将要求主线程继续为该连接对象读入数据
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                // 说明一行结束,将行结束的'\r''\n'都换成'\0''\0';
                /* 如此之后以该行的行首指针字符串指针，可以将该行作为一个字符串 */
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 对于'\r'之后还有任何字符且不为'\n'的情况都视为行格式非法
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            /*
            当前情况仅有一种可能出现检测正确，即上一种if情况中的第一种子情况出现后新读入数据
            此时第一个检测字符就会是'\n'
            */
            /*
             TODO：逻辑上，由于上一个if使得函数以OPEN形态结束时，checked_idx
             并未增加到后一位，而是保留在'\r'处，所以重启线程执行时，指针应当不存在
             会指到'\n'的情况。
            */
            if ((m_checked_idx > 1) && m_read_buf[m_read_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    } // for
    // 如果一直没有遍历到'/r'或'/n'直到check指针走到read指针，则当前行不完整
    return LINE_OPEN;
}

char *http_conn::getLine()
{
    // 即 &m_read_buf[m_start_line],返回行起始地址
    return m_read_buf + m_start_line;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::doRequest()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    if (m_method == GET)
    {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }
    else if (m_method == POST)
    {
        // 如果是POST，则需要分析url，因为POST携带的信息接在url尾部
        const char *op = strrchr(m_url, '/') + 1;
        LOG_DEBUG("--请求POST报文的操作字符为: %s", op);
        switch (*op)
        {
        case '0':
        {
            // '0'代表跳转到注册页面
            string next_url = "/register.html";
            strncpy(m_real_file + len, next_url.c_str(),
                    FILENAME_LEN - len - 1);
        }
        break;
        case '1':
        {
            // '1'代表跳转到登录页面
            string next_url = "/log.html";
            strncpy(m_real_file + len, next_url.c_str(),
                    FILENAME_LEN - len - 1);
        }
        break;
        case '2':
        {
            // '2'代表登录检测
            char usr_name[32], pwd[32];
            /*
                登录检测发送的POST请求中的请求体的内容格式固定如下：
                user=xxxx&password=****
                因此对该请求体做提取
            */
            int check_idx = 5; // 从5开始，跳过user=
            int copy_idx = 0;
            while (m_content[check_idx] != '&')
            {
                usr_name[copy_idx++] = m_content[check_idx++];
            }
            usr_name[copy_idx] = '\0';
            check_idx += 10; // 再跳10个字符，跳过&password=
            copy_idx = 0;
            while (m_content[check_idx] != '\0')
            {
                pwd[copy_idx++] = m_content[check_idx++];
            }
            pwd[copy_idx] = '\0';

            // 获取数据库连接，并利用连接查询post输入的用户名
            connection_pool_wrapper safe_connect(*m_db_connect_pool);
            MYSQL *conn = safe_connect.get_raw_connection();
            if (conn != NULL)
            {
                char queryBuf[256];
                snprintf(queryBuf, sizeof(queryBuf) - 1,
                         "SELECT * from user where username = '%s' AND password = '%s'",
                         usr_name, pwd);
                if (mysql_query(conn, queryBuf))
                {
                    LOG_INFO("--用户： %s,登录失败，失败原因:mysql查询失败", usr_name);
                    // mysqL查询失败，跳转到错误页面
                    strncpy(m_real_file + len, "/logError.html",
                            FILENAME_LEN - len - 1);
                    break;
                }
                MYSQL_RES *result = mysql_store_result(conn);
                if (mysql_fetch_row(result))
                {
                    LOG_INFO("--用户：%s ,登录成功", usr_name);
                    // 查询成功，则证明用户名登录成功
                    strncpy(m_real_file + len, "/welcome.html",
                            FILENAME_LEN - len - 1);
                    break;
                }
                else
                {
                    LOG_INFO("--用户： %s,登录失败，失败原因:密码不匹配", usr_name);
                    // 查询结果为空，则证明用户名和密码不匹配
                    strncpy(m_real_file + len, "/logError.html",
                            FILENAME_LEN - len - 1);
                    break;
                }
            }
            else
            {
                // 如果当前数据库连接池中没有连接，则服务器忙
                // 跳转到登录错误
                string next_url = "/logError.html";
                strncpy(m_real_file + len, next_url.c_str(),
                        FILENAME_LEN - len - 1);
                LOG_INFO("--用户： %s,登录失败，失败原因:当前数据库连接池中连接用完", usr_name);
            }
        }
        break;
        case '3':
        {
            char usr_name[32], pwd[32];
            /*
                注册检测发送的POST请求中的请求体的内容格式固定如下：
                user=xxxx&password=****
                因此以该形式对该请求体做提取
            */
            int check_idx = 5; // 从5开始，跳过user=
            int copy_idx = 0;
            while (m_content[check_idx] != '&')
            {
                usr_name[copy_idx++] = m_content[check_idx++];
            }
            usr_name[copy_idx] = '\0';
            check_idx += 10; // 再跳10个字符，跳过&password=
            copy_idx = 0;
            while (m_content[check_idx] != '\0')
            {
                pwd[copy_idx++] = m_content[check_idx++];
            }
            pwd[copy_idx] = '\0';
            // 获取数据库连接，并利用连接查询post输入的用户名
            connection_pool_wrapper safe_connect(*m_db_connect_pool);
            MYSQL *conn = safe_connect.get_raw_connection();
            if (conn != NULL)
            {
                char query_cmd[256];
                snprintf(query_cmd, sizeof(query_cmd) - 1,
                         "SELECT * from user where username = '%s'",
                         usr_name);
                mysql_query(conn, query_cmd);
                MYSQL_RES *result = mysql_store_result(conn);
                if (mysql_fetch_row(result))
                {
                    // 说明已有同名用户名存在
                    strncpy(m_real_file + len, "/registerError.html",
                            FILENAME_LEN - len - 1);
                    LOG_INFO("--用户： %s,注册失败，失败原因:注册用户名已存在", usr_name);
                    break;
                }
                else
                {
                    // 说明没有同名用户，则可以增加数据
                    snprintf(query_cmd, sizeof(query_cmd) - 1,
                             "INSERT INTO user(username,password) VALUES('%s','%s')",
                             usr_name, pwd);
                    if (mysql_query(conn, query_cmd) == 0)
                    {
                        // 注册成功
                        strncpy(m_real_file + len, "/log.html",
                                FILENAME_LEN - len - 1);
                        LOG_INFO("--用户： %s,注册成功！", usr_name);
                        break;
                    }
                    else
                    {
                        // 注册失败
                        strncpy(m_real_file + len, "/registerError.html",
                                FILENAME_LEN - len - 1);
                        LOG_INFO("--用户： %s,注册失败，失败原因:mysql增加数据失败", usr_name);
                        break;
                    }
                }
            }
            else
            {
                // 如果当前数据库连接池中没有连接，则服务器忙
                // 跳转到注册错误
                string next_url = "/registerError.html";
                strncpy(m_real_file + len, next_url.c_str(),
                        FILENAME_LEN - len - 1);
                LOG_INFO("--用户： %s,注册失败，失败原因:当前数据库连接池中连接用完", usr_name);
            }
        }
        break;
        default:
            break;
        }
    }

    /* unix系统函数调用 */
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    /*
    之所以使用内存映射而不是共享内存，是由于多线程环境下，我们需要每个线程
    拥有独立的虚拟内存，从而保证两个同时工作的线程之间的写缓存空间不冲突
    */
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
    return true;
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

// c语言中的可变参数函数,利用vsnprintf在wtire buf中格式写
bool http_conn::add_response(const char *format, ...)
{
    // 如果写的索引大于写缓冲区，则返回错误
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    // 操作参数
    va_list arg_list;
    // 改变格式
    va_start(arg_list, format);
    /*
        功能：往内存中以格式化参数进行格式写
        参数1：指明写缓存中写指针的位置
        参数2：能够写的长度
        参数3：格式
        参数4：参数列表
    */

    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    // 恢复格式
    va_end(arg_list);
    return true;
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type: %s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

void http_conn::unmap()
{

    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
