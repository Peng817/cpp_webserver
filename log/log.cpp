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

bool log::init(const char *file_name, int log_buf_size, int max_lines, int max_queue_size)
{
    m_log_buf_size = log_buf_size;
    m_max_lines = max_lines;
    /*
        生成日志文件，并打开
    */
    // 获取当前时间
    time_t cur = time(NULL);
    // 获取当前时间的tm结构体
    struct tm *tm_cur = localtime(&cur);
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
    //"a":追加到一个文件。写操作向文件末尾追加数据。如果文件不存在，则创建文件。
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        // 打开文件失败
        return false;
    }
    /*
        异步模式：设置阻塞队列的长度 > 0，
        同步模式: 应设置阻塞队列长度 = 0
    */
    if (max_queue_size > 0)
    {
        m_is_async = true;
        m_log_queue = new block_queue<std::string>(max_queue_size);
        pthread_t tid;
        // 模仿线程池的构建，对这部分代码进行优化
        if (pthread_create(&tid, NULL, work, NULL) != 0)
        {
            // 这部分资源其实也能交给析构函数释放，此处有点多余
            delete m_log_queue;
            m_log_queue = NULL;
            return false;
        }
        // 设置为脱离线程，让系统帮助回收线程
        if (pthread_detach(tid))
        {
            // 若脱离失败，则有责释放创建好的线程，和其他资源一起释放
            delete &tid;
            delete m_log_queue;
            m_log_queue = NULL;
            return false;
        }
        // 脱离后的线程无需管理，析构函数也不再考虑
    }

    // 由于日志类是单例模式，因此所有成员指针指向的数据都应该在堆区上
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size); // 缓存初始化
    m_today = tm_cur->tm_mday;
    return true;
}

void log::write_log(int level, const char *format, ...)
{
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
}

void log::change_log_level(int level)
{
    m_log_level = level;
}

log::log() : m_count(0), m_is_async(false), m_log_queue(NULL), m_buf(NULL)
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
        fclose(m_fp);
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
    /*
        假使有多个线程同时进行异步写，则对日志文件的访问
        应该是互斥的。刚好日志类中有准备好的成员互斥量供使用

        值得探讨的是多线程异步写日志，势必会造成日志行
        的错乱，这是多线程效率和内容易读性的trade off

        对于多线程异步写日志的错行内容，我们阅读时可以
        通过每行日志的记录时间来确定当前行记录的顺序

        如果要确保日志顺序没问题，则应使用单线程异步写日志
        如此，甚至对日志文件的访问都无需上锁。
        虽然本项目采取的最终方案是单线程异步方案，但是
        在实现对日志文件的访问还是进行了上锁的操作，这是为了多线程准备，
        虽然将多线程的代码配置用于单线程，这徒增了日志记录
        系统的开销，但是本项目主要以练习学习为主，则算是
        故意为之。
    */
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
    }
}
