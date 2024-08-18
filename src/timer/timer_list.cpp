#include "timer_list.h"
#include "../http/http_conn.h"

SortTimerList::SortTimerList()
{
    head = NULL;
    tail = NULL;
}
SortTimerList::~SortTimerList()
{
    UtilTimer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void SortTimerList::addTimer(UtilTimer *timer)
{
    if (!timer)
    {
        return;
    }
    if (!head)
    {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    addTimer(timer, head);
}
void SortTimerList::adjustTimer(UtilTimer *timer)
{
    if (!timer)
    {
        return;
    }
    UtilTimer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        addTimer(timer, head);
    }
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        addTimer(timer, timer->next);
    }
}
void SortTimerList::deleteTimer(UtilTimer *timer)
{
    if (!timer)
    {
        return;
    }
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
void SortTimerList::tick()
{
    if (!head)
    {
        return;
    }
    
    time_t cur = time(NULL);
    UtilTimer *tmp = head;
    while (tmp)
    {
        if (cur < tmp->expire)
        {
            break;
        }
        tmp->callback(tmp->userData);
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

void SortTimerList::addTimer(UtilTimer *timer, UtilTimer *lst_head)
{
    UtilTimer *prev = lst_head;
    UtilTimer *tmp = prev->next;
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

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setNonBlocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addFd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}

//信号处理函数
void Utils::signalHandler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipeFds[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addSignal(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timerHandler()
{
    m_timerList.tick();
    alarm(m_TIMESLOT);
}

void Utils::showError(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipeFds = 0;
int Utils::u_epollFd = 0;

class Utils;
void callback(ClientData *user_data)
{
    epoll_ctl(Utils::u_epollFd, EPOLL_CTL_DEL, user_data->sockFd, 0);
    assert(user_data);
    close(user_data->sockFd);
    HttpConn::g_userCount -- ;
}
