#ifndef LOG_H
#define LOG_H
#include <pthread.h>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include "block_queue.hpp"
class log
{
public:
    enum E_LOG_MODE
    {
        SYNC = 0,
        ASYNC
    };

    enum E_LOG_LEVEL
    {
        LEVEL_DEBUG = 0,
        LEVEL_INFO,
        LEVEL_WARN,
        LEVEL_ERROR
    };
    /*
        当前项目未指定c++版本，因此默认是旧版本c++
        使用加锁访问局部静态变量来使用懒汉模式
    */
    static log *get_instance();
    /*
        param:
            file_name: 日志拟设路径（绝对/相对均可）
            log_buf_size: 日志缓冲区大小
            max_lines: 日志最大行数
            max_queue_size: 协助日志记录的任务阻塞队列最大长度
        return(bool):
            ture: 初始化成功
            false: 初始化失败
    */
    bool init(int level, const char *file_name, int log_buf_size, int max_lines, int max_queue_size);
    /*
        以日志等级 + 格式化输入 生成日志行
        当以同步模式调用该函数，当前程序会直接将日志行输入到日志文件中
        当以异步模式调用该函数，当前程序将把日志行送入到阻塞队列中，
        由其他线程异步输入到日志文件中

        param:
            level: 记录等级
            file: 调用该函数记录来自的文件名(由宏生成)
            line: 调用该函数记录来自的文件名代码的具体代码所在行号(由宏生成)
            format,...: 格式化输入参数
    */
    void write_log(int level, const char *file, const int line, const char *format, ...);
    // 将文件指针所带的缓冲强制写入到文件中，可以在调用若干次记录函数后调用一次提高效率
    void file_flush();
    void set_log_level(int level);
    int get_log_level();

private:
    log();
    ~log();

    /*
        游双书P303："值得一提的是，在C++程序中使用pthread_creat时，该
        函数中第三个参数必须指向一个静态函数
        该work函数作为静态函数供所有线程进行工作
    */
    static void *work(void *args);
    /*
        异步写的内容已经存放在阻塞队列中，所以无需像同步写函数那样要接收内容
    */
    void async_write_log();

private:
    // 路径名
    char m_dir_name[128];
    // 日志文件名
    char m_log_name[128];
    // 临界资源：日志被打开后的c风格文件指针
    FILE *m_fp;
    // 日志的缓存区长度
    int m_log_buf_size;
    // 临界资源：日志一行的缓存区
    char *m_buf;
    // 日志的最大行数
    int m_max_lines;
    // 日志的阻塞队列的最大长度
    int m_max_queue_size;
    // 临界资源：当前日志文件的行数
    long long m_count;
    // 临界资源：当天的日志文件计数
    int m_file_count;
    // 日志的记录模式（同步false/异步true）
    bool m_is_async;
    // 临界资源：日志类最近一次更新的当天(montn-day)
    int m_today;
    // 日志记录等级，默认为最低级DEBUG级
    int m_log_level;
    // 异步线程号
    pthread_t m_async_thread;
    // 阻塞任务队列，一个任务即为一个string，表示一行记录
    block_queue<std::string> *m_log_queue;
    // 多线程访问单例模式上锁
    static locker m_mutex;
};

#define LOG_DEBUG(format, ...) log::get_instance()->write_log(log::LEVEL_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) log::get_instance()->write_log(log::LEVEL_INFO, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) log::get_instance()->write_log(log::LEVEL_WARN, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) log::get_instance()->write_log(log::LEVEL_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)

#endif