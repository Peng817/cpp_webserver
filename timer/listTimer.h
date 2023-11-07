#ifndef LIST_TIMER
#define LIST_TIMER

#include <time.h>
#include "../http_connect/http_conn.h"
class http_conn;

class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    // 超时时间，这里使用绝对时间
    time_t expire;
    // 回调函数指针
    void (*cb_func)(http_conn *);
    // 回调函数处理的对象指针
    http_conn *data;
    util_timer *prev;
    util_timer *next;
};

class sort_timer_lst
{

public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    ~sort_timer_lst();
    // 向类链表中插入timer
    void add_timer(util_timer *timer);
    // 删除指定节点
    void del_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    /*
    定时器链表的心跳函数，调用一次将把链表中所有ddl到期的节点
    的回调函数全部触发，并删除节点
    */
    void tick();

private:
    // 向类链表的子链表中插入timer
    void add_timer(util_timer *timer, util_timer *lst_head);

public:
    util_timer *head;
    util_timer *tail;
};
#endif