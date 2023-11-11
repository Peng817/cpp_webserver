#include "listTimer.h"

sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // 若链表头为空，则说明加入的timer是第一个timer
    if (!head)
    {
        head = tail = timer;
        return;
    }
    // 若加入的timer的ddl时间比当前有序链表中ddl最小的节点，即头节点还小
    // 则将timer作为头节点，之前的链表跟在timer之后
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    /*
    否则，加入的timer需要放的位置在head之后，该段逻辑可以复用于调整函数
    故抽象出来以便复用
    */
    add_timer(timer, head);
}

void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    printf("--delete 1 tiemr.\n");
    LOG_INFO("--delete 1 tiemr.");
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    time_t cur = time(NULL);
    util_timer *tmp = head;
    while (tmp)
    {
        /*
        如果循环到的tmp节点ddl还未到期
        由于链表有序，则说明之后的所有节点
        的ddl都没到期，则可以提前跳出循环
        */
        if (cur < tmp->expire)
        {
            break;
        }
        /*
        否则，迭代到的tmp节点已经到达ddl，
        则调用该tmp节点的回调函数
        并删除节点，由于逻辑简单，
        这里并没有使用del_timer，而是多写了一段逻辑
        */
        tmp->cb_func(tmp->data);
        util_timer *tmp2;
        tmp2 = tmp->next;
        del_timer(tmp);
        tmp = tmp2;
        // head = tmp->next;
        // if (head)
        // {
        //     head->prev = NULL;
        // }
        // delete tmp;
        // tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp)
    {
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}
