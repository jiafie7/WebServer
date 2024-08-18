#ifndef TIMER_LIST
#define TIMER_LIST

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

class UtilTimer;

struct ClientData
{
    sockaddr_in address;
    int sockFd;
    UtilTimer *timer;
};

class UtilTimer
{
public:
    UtilTimer() : prev(NULL), next(NULL) {}

public:
    time_t expire;
    
    void (* callback)(ClientData *);
    ClientData *userData;
    UtilTimer *prev;
    UtilTimer *next;
};

class SortTimerList
{
public:
    SortTimerList();
    ~SortTimerList();

    void addTimer(UtilTimer *timer);
    void adjustTimer(UtilTimer *timer);
    void deleteTimer(UtilTimer *timer);
    void tick();

private:
    void addTimer(UtilTimer *timer, UtilTimer *lst_head);

    UtilTimer *head;
    UtilTimer *tail;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setNonBlocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addFd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void signalHandler(int sig);

    //设置信号函数
    void addSignal(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timerHandler();

    void showError(int connfd, const char *info);

public:
    static int *u_pipeFds;
    SortTimerList m_timerList;
    static int u_epollFd;
    int m_TIMESLOT;
};

void callback(ClientData *user_data);

#endif
