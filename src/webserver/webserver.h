#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "../threadpool/threadpool.h"
#include "../http/http_conn.h"

const int MAX_FILE_DESCRIPTORS = 65536;  // 最大文件描述符
const int MAX_EVENT_COUNT = 10000;       // 最大事件数
const int TIME_SLOT = 5;                 // 最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port, const std::string& user, const std::string& password, const std::string& databaseName,
              int logWriteMethod, int enableLinger, int triggerMode, int sqlConnectionPoolSize,
              int threadPoolSize, int logStatus, int actorModel);

    void setupThreadPool();
    void setupDatabaseConnectionPool();
    void setupLogging();
    void configureTriggerMode();
    void startListening();
    void startEventLoop();
    void addTimer(int connectionFd, const struct sockaddr_in& clientAddress);
    void adjustTimer(UtilTimer* timer);
    void handleTimer(UtilTimer* timer, int socketFd);
    bool handleClientData();
    bool handleSignals(bool& timeout, bool& stopServer);
    void handleRead(int socketFd);
    void handleWrite(int socketFd);

public:
    int m_port;
    char* m_rootDirectory;
    int m_logWriteMethod;
    int m_logStatus;
    int m_actorModel;

    int m_pipeFds[2];
    int m_epollFd;
    HttpConn* m_users;

    // Database
    ConnectionPool* m_connectionPool;
    std::string m_databaseUser;         // Database username
    std::string m_databasePassword;     // Database password
    std::string m_databaseName;         // Database name
    int m_sqlConnectionPoolSize;


    // Thread pool
    ThreadPool<HttpConn>* m_threadPool;
    int m_threadPoolSize;

    // epoll_event
    epoll_event m_events[MAX_EVENT_COUNT];
    
    int m_listenFd;
    int m_enableLinger;
    int m_triggerMode;
    int m_listenTriggerMode;
    int m_connectionTriggerMode;

    // Timer
    ClientData* m_userTimers;
    Utils m_utils;
};
#endif
