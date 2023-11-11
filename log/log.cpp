#include "log.h"
// 定义静态成员变量
locker log::m_mutex;

log *log::get_instance()
{
    m_mutex.lock();
    static log instance;
    m_mutex.unlock();
    return &instance;
}

void get_next_log_full_name(char *next_full_name, size_t size, char *full_name, long next_num)
{
    // 检查日志路径是否带.xxx格式
    const char *p = strrchr(full_name, '.');
    if (p == NULL)
    {
        // 如果路径未找到.，则说明没有格式后缀，直接贴数字即可
        snprintf(next_full_name, size, "%s_%lld", full_name, next_num);
    }
    else
    {
        // 如果日志路径有格式后缀，则将.后缀名和前面的字符分隔开
        char pre[256] = {0};
        strncpy(pre, full_name, p - full_name);
        snprintf(next_full_name, size, "%s_%lld.%s", pre, next_num, p + 1);
    }
}

bool log::init(int level, const char *file_name, int log_buf_size, int max_lines, int max_queue_size)
{
    m_log_buf_size = log_buf_size;
    m_max_lines = max_lines;
    m_log_level = level;
    m_count = 0;
    m_file_count = 0;
    /*
        生成日志文件，并打开
    */
    // 获取当前时间
    struct timeval cur = {0, 0};
    // 使用getttimeofday可以获得微秒信息
    gettimeofday(&cur, NULL);
    /*
        time(), localtime(&time),struct tm 来自于 c语言
        struct tm {
        int tm_sec;     // seconds after the minute - [0,59]
        int tm_min;     // minutes after the hour - [0,59]
        int tm_hour;    // hours since midnight - [0,23]
        int tm_mday;    // day of the month - [1,31]
        int tm_mon;     // months since January - [0,11]
        int tm_year;    // years since 1900
        int tm_wday;    // days since Sunday - [0,6]
        int tm_yday;    // days since January 1 - [0,365]
        int tm_isdst;   // daylight savings time flag
        };

    */
    // 获取当前时间的tm结构体
    struct tm *tm_cur = localtime(&cur.tv_sec);
    m_today = tm_cur->tm_mday;

    // 日志文件创建的绝对路径/相对路径
    char log_full_name[256] = {0};
    // 从后往前找到参数filename中第一个/的位置
    const char *p = strrchr(file_name, '/');
    if (p == NULL)
    {
        // 若输入的文件名中没有/，则代表输入的filename是相对路径
        // 则log_full_name记录的也将是相对路径，日志文件名=当前时间+输入文件名
        strcpy(m_log_name, file_name);
        strcpy(m_dir_name, "./");
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s",
                 tm_cur->tm_year + 1900, tm_cur->tm_mon + 1,
                 tm_cur->tm_mday, file_name);
    }
    else
    {
        // 若输入的文件名中有/,则表示输入的filename是绝对路径
        // 则log_full_name记录的也将是绝对路径，
        // 日志文件名=输入文件名所在文件夹+当前时间+输入文件名
        strcpy(m_log_name, p + 1);                         // p+1代表/之后第一个字符
        strncpy(m_dir_name, file_name, p - file_name + 1); // p-file_name+1是文件所在文件夹的路径长度
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", m_dir_name,
                 tm_cur->tm_year + 1900, tm_cur->tm_mon + 1,
                 tm_cur->tm_mday, m_log_name);
    }

    // ******临界
    m_mutex.lock();
    //"a":追加到一个文件。写操作向文件末尾追加数据。如果文件不存在，则创建文件。
    if (m_fp)
    {
        file_flush();
        fclose(m_fp);
    }

    m_fp = fopen(log_full_name, "a+");
    if (m_fp == NULL)
    {
        // 打开文件失败
        return false;
    }
    // 由于日志类是单例模式，因此所有成员指针指向的数据都应该在堆区上
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size); // 缓存初始化
    // TODO:可以优化检查，以文件为单位检查而不是逐行检查
    while (fgets(m_buf, m_log_buf_size, m_fp))
    {
        m_count++;
    }
    if (m_count >= m_max_lines)
    {
        char next_log_full_name[256] = {0};
        while (1)
        {
            m_file_count++;
            get_next_log_full_name(next_log_full_name, 255, log_full_name, m_file_count);
            fclose(m_fp);
            // 打开文件，并继续检查
            m_fp = fopen(next_log_full_name, "a+");
            m_count = 0;

            if (m_fp == NULL)
            {
                return false;
            }
            while (fgets(m_buf, m_log_buf_size, m_fp))
            {
                m_count++;
            }
            // 如果当前日志文件的记录也满了，则继续检查下一个文件
            if ((m_count >= m_max_lines))
            {
                continue;
            }
            else
            {
                // 否则当前日志文件还可以继续写
                break;
            }
        }
    }
    memset(m_buf, '\0', m_log_buf_size); // 缓存初始化
    m_mutex.unlock();
    //******出临界区

    m_today = tm_cur->tm_mday;
    /*
        异步模式：设置阻塞队列的长度 > 0，
        同步模式: 应设置阻塞队列长度 = 0
    */
    std::string title;
    title += "\n";
    title += "                       _    _                \n";
    title += "  _ __ ___   ___  _ __| | _| |    ___   __ _ \n";
    title += " | '_ ` _ \\ / _ \\| '__| |/ / |   / _ \\ / _` |       _ _/|\n";
    title += " | | | | | | (_) | |  |   <| |__| (_) | (_| |       \\o.0|\n";
    title += " |_| |_| |_|\\___/|_|  |_|\\_\\_____\\___/ \\__, |      =(___)=\n";
    title += "                                       |___/ \n";
    if (max_queue_size > 0)
    {
        if (m_async_thread == 0)
        {
            m_is_async = true;
            m_log_queue = new block_queue<std::string>(max_queue_size);
            // 模仿线程池的构建，对这部分代码进行优化
            if (pthread_create(&m_async_thread, NULL, work, NULL) != 0)
            {
                // 这部分资源其实也能交给析构函数释放，此处有点多余
                delete m_log_queue;
                m_log_queue = NULL;
                return false;
            }
        }
        printf("--日志异步模式开启,异步线程tid:%ld\n", m_async_thread);
        LOG_INFO("%s", title.c_str());
        LOG_DEBUG("--日志异步模式开启,异步线程tid:%ld", m_async_thread);
    }
    else
    {
        printf("--日志同步模式开启\n");
        LOG_INFO("%s", title.c_str());
        LOG_DEBUG("--日志同步模式开启");
    }

    return true;
}

void log::write_log(int level, const char *file, const int line, const char *format, ...)
{
    if (level < m_log_level)
    {
        return;
    }
    // 获取使用当前函数时的时间
    struct timeval cur = {0, 0};
    // 使用getttimeofday可以获得微秒信息
    gettimeofday(&cur, NULL);
    struct tm *tm_cur = localtime(&cur.tv_sec);
    va_list args;
    va_start(args, format);
    // 是否有必要特地使用string 来装载本次m_buf的内容？
    std::string log_str;
    /*
        此处临界区的必要性：对于日志类的行缓存必须保持互斥访问，
        这是因为当前日志类将处在多线程项目环境下，可能有多个线程
        同时调用日志类记录日志到日志类的行缓存中，在这种情况下，
        必须保证日志的读取到缓存，从缓存写入到日志文件或阻塞队列
        的过程是原子化，否则存在缓存读取后，在写入前被别的线程修改
        缓存的可能性。
    */
    //******临界区
    m_mutex.lock();
    char level_str[16] = {0};
    switch (level)
    {
    case LEVEL_DEBUG:
        strcpy(level_str, "DEBUG");
        break;
    case LEVEL_INFO:
        strcpy(level_str, "INFO");
        break;
    case LEVEL_WARN:
        strcpy(level_str, "WARN");
        break;
    case LEVEL_ERROR:
        strcpy(level_str, "ERROR");
        break;
    default:
        strcpy(level_str, "UNKOWN");
        break;
    }
    // (时间+日志等级)前缀 格式化输入到m_buf缓存中
    // snprintf成功返回写入缓存的字符总数，其中不包括结尾的null字符
    int prefix_size =
        snprintf(m_buf, 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld[%s]file:%s:%d: ",
                 tm_cur->tm_year + 1900, tm_cur->tm_mon + 1,
                 tm_cur->tm_mday, tm_cur->tm_hour, tm_cur->tm_min,
                 tm_cur->tm_sec, cur.tv_usec, level_str, file, line);
    // (可变参数)内容 可变参格式化输入到m_buf缓存中
    int content_size =
        vsnprintf(m_buf + prefix_size, m_log_buf_size - prefix_size - 2,
                  format, args);
    m_buf[prefix_size + content_size] = '\n';
    m_buf[prefix_size + content_size + 1] = '\0';
    // TODO:看到下面这行拷贝放弃使用sting类型的log_str,直接将m_buf作为字符串输入到日志中
    log_str = m_buf; // 调用string容器的关于字符串的赋值函数，拷贝一次
    // 写入日志文件或写入阻塞队列
    if (m_is_async)
    {
        m_log_queue->push(log_str);
    }
    else
    {
        // 同步模式，则是当前进程写入日志文件，需要上锁
        fputs(log_str.c_str(), m_fp);
    }
    m_mutex.unlock();
    //******出临界区
    va_end(args);

    /*
        TODO:这段逻辑有点小bug但不影响正确运行，
        主要是因为输入一次的记录可能不止占一行导致的：
        一个日志文件最多8000条日志，不止8000行字符

        两段临界区之间没有共享的临界量，应该不会有关联
    */
    //******临界区
    m_mutex.lock();
    // 新增一行记录，并检查是否要新开辟一个日志文件
    m_count++;
    if ((m_today != tm_cur->tm_mday) || (m_count >= m_max_lines))
    {
        char log_new_full_name[256] = {0};
        char time_str[16] = {0};
        // 格式化生成 用于插入新的日志文件名的时间字符串
        snprintf(time_str, 16, "%d_%02d_%02d_", tm_cur->tm_year + 1900,
                 tm_cur->tm_mon + 1, tm_cur->tm_mday);
        if (m_today != tm_cur->tm_mday)
        {
            // 如果今天不是最近一次更新的日,即服务器工作跨天了，则创建今天的日志，更新m_today和m_count
            snprintf(log_new_full_name, 255, "%s%s%s", m_dir_name, time_str, m_log_name);
            m_today = tm_cur->tm_mday;
            m_count = 0;
            m_file_count = 1;
        }
        else
        {
            // 如果超过了最大行，则在基础日志全名之上加上当日的日志文件计数号
            char log_full_name[256] = {0};
            m_file_count++;
            m_count = 0;
            snprintf(log_full_name, 255, "%s%s%s", m_dir_name, time_str, m_log_name);
            get_next_log_full_name(log_new_full_name, 255, log_full_name, m_file_count);
        }
        // 修改日志类维护的打开文件指针，需要上锁
        fflush(m_fp);
        fclose(m_fp);
        m_fp = fopen(log_new_full_name, "a");
    }
    m_mutex.unlock();
}

void log::file_flush()
{
    if (m_is_async)
    {
        m_log_queue->flush();
    }
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}

int log::get_log_level()
{
    return m_log_level;
}

void log::set_log_level(int level)
{
    m_log_level = level;
}

log::log() : m_count(0), m_file_count(1), m_is_async(false),
             m_log_queue(NULL), m_buf(NULL), m_async_thread(0)
{
    /*
        默认构造函数将使得日志记录为0，日志记录模式默认为同步,
        所有成员指针需要初始化为指向NULL，便于之后析构函数判断
        指针是否指向了实际存在的数据，如果有，则需要释放
        对于未提及的成员变量的初始化则全部交给无参定义
    */
}

log::~log()
{
    if (m_async_thread != 0)
    {
        m_mutex.lock();
        while (m_log_queue && !m_log_queue->empty())
        {
            // 使得任务队列中的任务全部完成
            m_log_queue->flush();
        }
        // 关闭队列将使得异步线程执行的函数结束
        m_log_queue->close();
        m_mutex.unlock();
        pthread_join(m_async_thread, NULL);
        // printf("--日志异步线程已回收,tid=%ld\n", m_async_thread);
    }
    // 如果阻塞队列指针非空，代表存在一个new出来的阻塞队列，需要及时释放
    if (!m_log_queue)
    {
        delete m_log_queue;
    }
    // 如果日志缓存指针非空，代表在init过程中在堆区申请了日志缓存空间，需要及时释放
    if (!m_buf)
    {
        delete[] m_buf;
    }
    // 关闭对象打开的文件指针
    if (m_fp != NULL)
    {
        file_flush();
        m_mutex.lock();
        fclose(m_fp);
        m_mutex.unlock();
    }
}

void *log::work(void *args)
{
    // 线程的工作内容就是执行日志实例对象的异步写日志函数
    log::get_instance()->async_write_log();
    return args;
}

void log::async_write_log()
{
    std::string single_log;
    while (m_log_queue->pop(single_log))
    {
        /*
            pop为false的条件只有条件变量本身出了问题，
            一般都将不断从阻塞队列取记录，如果队列空了，
            则导致线程阻塞，直到有新的内容被push，从而唤醒阻塞
        */
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        m_mutex.unlock();
        //  printf("--tid:%ld log async-thread finish 1 job.\n", pthread_self());
    }
}
