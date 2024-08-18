#include "webserver.h"

WebServer::WebServer()
{
    // Initialize HTTP connection objects
    m_users = new HttpConn[MAX_FILE_DESCRIPTORS];

    // Root directory path
    char serverPath[200];
    getcwd(serverPath, 200);

    const char rootDirectory[] = "/../static";
    m_rootDirectory = static_cast<char*>(malloc(strlen(serverPath) + strlen(rootDirectory) + 1));
    strcpy(m_rootDirectory, serverPath);
    strcat(m_rootDirectory, rootDirectory);

    // Initialize timers
    m_userTimers = new ClientData[MAX_FILE_DESCRIPTORS];
}

WebServer::~WebServer()
{
    close(m_epollFd);
    close(m_listenFd);
    close(m_pipeFds[1]);
    close(m_pipeFds[0]);
    delete[] m_users;
    delete[] m_userTimers;
    delete m_threadPool;
}

void WebServer::init(int port, const std::string& user, const std::string& password, const std::string& databaseName,
                           int logWriteMethod, int enableLinger, int triggerMode, int sqlConnectionPoolSize,
                           int threadPoolSize, int logStatus, int actorModel)
{
    m_port = port;
    m_databaseUser = user;
    m_databasePassword = password;
    m_databaseName = databaseName;
    m_sqlConnectionPoolSize = sqlConnectionPoolSize;
    m_threadPoolSize = threadPoolSize;
    m_logWriteMethod = logWriteMethod;
    m_enableLinger = enableLinger;
    m_triggerMode = triggerMode;
    m_logStatus = logStatus;
    m_actorModel = actorModel;
}


void WebServer::configureTriggerMode()
{
    switch (m_triggerMode)
    {
        case 0: // LT + LT
            m_listenTriggerMode = 0;
            m_connectionTriggerMode = 0;
            break;
        case 1: // LT + ET
            m_listenTriggerMode = 0;
            m_connectionTriggerMode = 1;
            break;
        case 2: // ET + LT
            m_listenTriggerMode = 1;
            m_connectionTriggerMode = 0;
            break;
        case 3: // ET + ET
            m_listenTriggerMode = 1;
            m_connectionTriggerMode = 1;
            break;
        default:
            break;
    }
}


void WebServer::setupLogging()
{
    if (m_logStatus == 0)
    {
        // Initialize logging
        if (m_logWriteMethod == 1)
            Log::getInstance()->init("./ServerLog", m_logStatus, 2000, 800000, 800);
        else
            Log::getInstance()->init("./ServerLog", m_logStatus, 2000, 800000, 0);
    }
}
void WebServer::setupDatabaseConnectionPool()
{
    // Initialize database connection pool
    m_connectionPool = ConnectionPool::getInstance();
    m_connectionPool->init("localhost", m_databaseUser, m_databasePassword, m_databaseName, 3306, m_sqlConnectionPoolSize, m_logStatus);

    // Initialize database result table
    m_users->initMysqlResult(m_connectionPool);
}

void WebServer::setupThreadPool()
{
    // Initialize thread pool
    m_threadPool = new ThreadPool<HttpConn>(m_actorModel, m_connectionPool, m_threadPoolSize);
}


void WebServer::startListening()
{
    // Set up listening socket
    m_listenFd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenFd >= 0);

    // Configure graceful connection closure
    struct linger lingerOpt = { m_enableLinger, 1 };
    setsockopt(m_listenFd, SOL_SOCKET, SO_LINGER, &lingerOpt, sizeof(lingerOpt));

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int reuseAddr = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr));
    int ret = bind(m_listenFd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenFd, 5);
    assert(ret >= 0);

    m_utils.init(TIME_SLOT);

    // Create epoll event table
    m_epollFd = epoll_create(5);
    assert(m_epollFd != -1);

    m_utils.addFd(m_epollFd, m_listenFd, false, m_listenTriggerMode);
    HttpConn::g_epollFd = m_epollFd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipeFds);
    assert(ret != -1);
    m_utils.setNonBlocking(m_pipeFds[1]);
    m_utils.addFd(m_epollFd, m_pipeFds[0], false, 0);

    m_utils.addSignal(SIGPIPE, SIG_IGN);
    m_utils.addSignal(SIGALRM, m_utils.signalHandler, false);
    m_utils.addSignal(SIGTERM, m_utils.signalHandler, false);

    alarm(TIME_SLOT);

    // Initialize utility class for signal and file descriptor operations
    Utils::u_pipeFds = m_pipeFds;
    Utils::u_epollFd = m_epollFd;
}


void WebServer::addTimer(int connectionFd, const struct sockaddr_in& clientAddress)
{
    m_users[connectionFd].init(connectionFd, clientAddress, m_rootDirectory, m_connectionTriggerMode, m_logStatus, m_databaseUser, m_databasePassword, m_databaseName);

    // Initialize client data and create timer
    m_userTimers[connectionFd].address = clientAddress;
    m_userTimers[connectionFd].sockFd = connectionFd;
    UtilTimer* timer = new UtilTimer;
    timer->userData = &m_userTimers[connectionFd];
    timer->callback = callback;
    time_t currentTime = time(nullptr);
    timer->expire = currentTime + 3 * TIME_SLOT;
    m_userTimers[connectionFd].timer = timer;
    m_utils.m_timerList.addTimer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjustTimer(UtilTimer* timer)
{
    time_t currentTime = time(nullptr);
    timer->expire = currentTime + 3 * TIME_SLOT;
    m_utils.m_timerList.adjustTimer(timer);

    LOG_INFO(m_logStatus, "%s", "Timer adjusted");
}


void WebServer::handleTimer(UtilTimer* timer, int socketFd)
{
    timer->callback(&m_userTimers[socketFd]);
    if (timer)
    {
        m_utils.m_timerList.deleteTimer(timer);
    }

    LOG_INFO(m_logStatus, "Closed socket %d", m_userTimers[socketFd].sockFd);
}


bool WebServer::handleClientData()
{
    struct sockaddr_in clientAddress;
    socklen_t clientAddrLength = sizeof(clientAddress);
    if (m_listenTriggerMode == 0)
    {
        int connectionFd = accept(m_listenFd, (struct sockaddr *)&clientAddress, &clientAddrLength);
        if (connectionFd < 0)
        {
            LOG_ERROR(m_logStatus, "%s: errno is %d", "Accept error", errno);
            return false;
        }
        if (HttpConn::g_userCount >= MAX_FILE_DESCRIPTORS)
        {
            m_utils.showError(connectionFd, "Internal server busy");
            LOG_ERROR(m_logStatus, "%s", "Internal server busy");
            return false;
        }
        addTimer(connectionFd, clientAddress);
    }
    else
    {
        while (true)
        {
            int connectionFd = accept(m_listenFd, (struct sockaddr *)&clientAddress, &clientAddrLength);
            if (connectionFd < 0)
            {
                LOG_ERROR(m_logStatus, "%s: errno is %d", "Accept error", errno);
                break;
            }
            if (HttpConn::g_userCount >= MAX_FILE_DESCRIPTORS)
            {
                m_utils.showError(connectionFd, "Internal server busy");
                LOG_ERROR(m_logStatus, "%s", "Internal server busy");
                break;
            }
            addTimer(connectionFd, clientAddress);
        }
        return false;
    }
    return true;
}

bool WebServer::handleSignals(bool& timeout, bool& stopServer)
{
    int ret = 0;
    int signal;
    char signals[1024];
    ret = recv(m_pipeFds[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
                case SIGALRM:
                    timeout = true;
                    break;
                case SIGTERM:
                    stopServer = true;
                    break;
                default:
                    break;
            }
        }
    }
    return true;
}


void WebServer::handleRead(int socketFd)
{
    UtilTimer* timer = m_userTimers[socketFd].timer;

    if (m_actorModel == 1)
    {
        if (timer)
        {
            adjustTimer(timer);
        }

        m_threadPool->append(m_users + socketFd, 0);

        while (true)
        {
            if (m_users[socketFd].isImproved == 1)
            {
                if (m_users[socketFd].timerFlag == 1)
                {
                    handleTimer(timer, socketFd);
                    m_users[socketFd].timerFlag = 0;
                }
                m_users[socketFd].isImproved = 0;
                break;
            }
        }
    }
    else
    {
        if (m_users[socketFd].readFromSocket())
        {
            LOG_INFO(m_logStatus, "deal with the client(%s)", inet_ntoa(m_users[socketFd].getAddress()->sin_addr));
            m_threadPool->appendP(m_users + socketFd);
            
            if (timer)
            {
                adjustTimer(timer);
            }
        }
        else
        {
            handleTimer(timer, socketFd);
        }
    }
}

void WebServer::handleWrite(int socketFd)
{
    UtilTimer* timer = m_userTimers[socketFd].timer;
    if (m_actorModel == 1)
    {
        if (timer)
        {
            adjustTimer(timer);
        }
        m_threadPool->append(m_users + socketFd, 1);
        while (true)
        {
            if (m_users[socketFd].isImproved == 1)
            {
                if (m_users[socketFd].timerFlag == 1)
                {
                    handleTimer(timer, socketFd);
                    m_users[socketFd].timerFlag = 0;
                }
                m_users[socketFd].isImproved = 0;
                break;
            }
        }
    }
    else
    {
        if (m_users[socketFd].writeToSocket())
        {
            LOG_INFO(m_logStatus, "Data sent to client %s", inet_ntoa(m_users[socketFd].getAddress()->sin_addr));
            if (timer)
            {
                adjustTimer(timer);
            }
        }
        else
        {
            handleTimer(timer, socketFd);
        }
    }
}

void WebServer::startEventLoop()
{
    bool timeout = false;
    bool stopServer = false;
    while (!stopServer)
    {
        int eventCount = epoll_wait(m_epollFd, m_events, MAX_EVENT_COUNT, -1);
        if (eventCount < 0 && errno != EINTR)
        {
            LOG_ERROR(m_logStatus, "%s", "Epoll failure");
            break;
        }

        for (int i = 0; i < eventCount; ++i)
        {
            int socketFd = m_events[i].data.fd;

            if (socketFd == m_listenFd)
            {
                bool success = handleClientData();
                if (!success)
                    continue;
            }
            else if (m_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                UtilTimer* timer = m_userTimers[socketFd].timer;
                handleTimer(timer, socketFd);
            }
            else if ((socketFd == m_pipeFds[0]) && (m_events[i].events & EPOLLIN))
            {
                bool success = handleSignals(timeout, stopServer);
                if (!success)
                    continue;
            }
            else if (m_events[i].events & EPOLLIN)
            {
                handleRead(socketFd);
            }
            else if (m_events[i].events & EPOLLOUT)
            {
                handleWrite(socketFd);
            }
        }
        if (timeout)
        {
            m_utils.timerHandler();
            LOG_INFO(m_logStatus, "%s", "Timer tick");
            timeout = false;
        }
    }
}
