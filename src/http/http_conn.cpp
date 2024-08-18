#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>
#include <utility>

// 定义http响应状态信息
const char *HTTP_STATUS_OK_TITLE = "OK";
const char *HTTP_STATUS_BAD_REQUEST_TITLE = "Bad Request";
const char *HTTP_STATUS_BAD_REQUEST_MESSAGE = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *HTTP_STATUS_FORBIDDEN_TITLE = "Forbidden";
const char *HTTP_STATUS_FORBIDDEN_MESSAGE = "You do not have permission to access the file from this server.\n";
const char *HTTP_STATUS_NOT_FOUND_TITLE = "Not Found";
const char *HTTP_STATUS_NOT_FOUND_MESSAGE = "The requested file was not found on this server.\n";
const char *HTTP_STATUS_INTERNAL_ERROR_TITLE = "Internal Error";
const char *HTTP_STATUS_INTERNAL_ERROR_MESSAGE = "There was an unusual problem serving the requested file.\n";

Locker m_lock;
std::map<std::string, std::string> m_users;

void HttpConn::initMysqlResult(ConnectionPool *connPool)
{
    // Obtain a connection from the connection pool
    MYSQL *mysql = NULL;
    ConnectionRAII mysqlcon(&mysql, connPool);

    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR(m_logStatus, "SELECT error:%s\n", mysql_error(mysql));
        return;
    }

    // Store the result set
    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result)
    {
        LOG_ERROR(m_logStatus, "Failed to store result: %s\n", mysql_error(mysql));
        return;
    }

    // Populate the map with usernames and passwords        
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)))
    {
        string username(row[0] ? row[0] : "");
        std::cout << username << '\n';
        string password(row[1] ? row[1] : "");
        std::cout << password << '\n';
        m_users[username] = password;
    }

    mysql_free_result(result);
}

// Set fd nonblocking
int setNonBlocking(int fd)
{
    int oldOptions = fcntl(fd, F_GETFL);
    int newOptions = oldOptions | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOptions);
    return oldOptions;
}

// Register file descriptor with the ET mode, and optionally EPOLLONESHOT
void addFd(int epollFd, int fd, bool oneShot, int triggerMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (triggerMode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (oneShot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}

// Remove file descriptor from the epoll instance
void removeFd(int epollFd, int fd)
{
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// Modify event to EPOLLONESHOT
void modFd(int epollFd, int fd, int events, int triggerMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (triggerMode == 1)
        event.events = events | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = events | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event);
}

int HttpConn::g_userCount = 0;
int HttpConn::g_epollFd = -1;


// Close the connection and decrement the user count
void HttpConn::closeConn(bool realClose)
{
    if (realClose && (m_socketFd != -1))
    {
        printf("close %d\n", m_socketFd);
        removeFd(g_epollFd, m_socketFd);
        m_socketFd = -1;
        -- g_userCount;
    }
}

// Initialize the connection, with socket address provided externally
void HttpConn::init(int socketFd, const sockaddr_in &address, char *docRoot, int triggerMode,
                     int logStatus, string user, string password, string databaseName)
{
    m_socketFd = socketFd;
    m_address = address;

    addFd(g_epollFd, socketFd, true, triggerMode);
    ++ g_userCount;

    // Potential issues include incorrect root directory, HTTP response format errors, or empty file content
    m_docRoot = docRoot;
    m_triggerMode = triggerMode;
    m_logStatus = logStatus;

    strcpy(m_user, user.c_str());
    strcpy(m_password, password.c_str());
    strcpy(m_databaseName, databaseName.c_str());

    reset();
}

// Initialize a new accepted connection
// Default CheckState is set to analyze request line state
void HttpConn::reset()
{
    mysql = NULL;
    m_bytesToSend = 0;
    m_bytesHaveSent = 0;
    m_checkState = CHECK_STATE_REQUEST_LINE;
    m_keepAlive = false;
    m_method = GET;
    m_url = nullptr;
    m_version = nullptr;
    m_contentLength = 0;
    m_host = nullptr;
    m_startLine = 0;
    m_checkedIndex = 0;
    m_readIndex = 0;
    m_writeIndex = 0;
    m_isCgi = 0;

    requestState = 0;
    timerFlag = 0;
    isImproved = 0;

    memset(m_readBuffer, '\0', MAX_READ_BUFFER_SIZE);
    memset(m_writeBuffer, '\0', MAX_WRITE_BUFFER_SIZE);
    memset(m_realFile, '\0', MAX_FILENAME_LENGTH);
}

// 从状态机，分析一行内容
// 返回值为行的读取状态，LINE_OK、LINE_BAD、LINE_OPEN
HttpConn::LineStatus HttpConn::parseLine()
{
    char currentChar;
    for (; m_checkedIndex < m_readIndex; ++m_checkedIndex)
    {
        currentChar = m_readBuffer[m_checkedIndex];
        if (currentChar == '\r')
        {
            if ((m_checkedIndex + 1) == m_readIndex)
                return LINE_OPEN;
            else if (m_readBuffer[m_checkedIndex + 1] == '\n')
            {
                m_readBuffer[m_checkedIndex++] = '\0';
                m_readBuffer[m_checkedIndex++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (currentChar == '\n')
        {
            if (m_checkedIndex > 1 && m_readBuffer[m_checkedIndex - 1] == '\r')
            {
                m_readBuffer[m_checkedIndex - 1] = '\0';
                m_readBuffer[++ m_checkedIndex] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}


// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool HttpConn::readFromSocket()
{
    if (m_readIndex >= MAX_READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytesRead = 0;

    // LT read mode
    if (m_triggerMode == 0)
    {
        bytesRead = recv(m_socketFd, m_readBuffer + m_readIndex, MAX_READ_BUFFER_SIZE - m_readIndex, 0);
        if (bytesRead > 0)
        {
            m_readIndex += bytesRead;
            return true;
        }
        return false;
    }
    // ET read mode
    else
    {
        while (true)
        {
            bytesRead = recv(m_socketFd, m_readBuffer + m_readIndex, MAX_READ_BUFFER_SIZE - m_readIndex, 0);
            if (bytesRead == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytesRead == 0)
            {
                return false;
            }
            m_readIndex += bytesRead;
        }
        return true;
    }
}


// 解析http请求行，获得请求方法，目标url及http版本号
HttpConn::HttpCode HttpConn::parseRequestLine(char *text)
{
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        m_isCgi = 1;
    }
    else
        return BAD_REQUEST;

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    // Default to "index.html" if URL is "/"
    if (strlen(m_url) == 1)
        strcat(m_url, "index.html");

    m_checkState = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求头部的一行
HttpConn::HttpCode HttpConn::parseHeaders(char *text)
{
    if (text[0] == '\0')
    {
        if (m_contentLength != 0)
        {
            m_checkState = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_keepAlive = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_contentLength = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO(m_logStatus, "Unknown header: %s", text);
    }
    return NO_REQUEST;
}

// 解析http请求正文
HttpConn::HttpCode HttpConn::parseContent(char *text)
{
    if (m_readIndex >= (m_contentLength + m_checkedIndex))
    {
        text[m_contentLength] = '\0';
        // In POST requests, the last part contains the input username and password
        m_requestData = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HttpConn::HttpCode HttpConn::processRead(ConnectionPool* connPool)
{
    LineStatus lineStatus = LINE_OK;
    HttpCode result = NO_REQUEST;
    char *line = nullptr;

    while ((m_checkState == CHECK_STATE_CONTENT && lineStatus == LINE_OK) || ((lineStatus = parseLine()) == LINE_OK))
    {
        line = currentLine();
        m_startLine = m_checkedIndex;
        LOG_INFO("%s", line);

        switch (m_checkState)
        {
        case CHECK_STATE_REQUEST_LINE:
        {
            result = parseRequestLine(line);
            if (result == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            result = parseHeaders(line);
            if (result == BAD_REQUEST)
                return BAD_REQUEST;
            else if (result == GET_REQUEST)
            {
                return generateRequest(connPool);
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            result = parseContent(line);
            if (result == GET_REQUEST)
                return generateRequest(connPool);
            lineStatus = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

void HttpConn::concatUrl(int length, const char* url)
{
    char* tempUrl = (char*)malloc(sizeof(char) * 200);
    strcpy(tempUrl, url);
    strncpy(m_realFile + length, tempUrl, strlen(tempUrl));
    free(tempUrl);
}

HttpConn::HttpCode HttpConn::generateRequest(ConnectionPool* connPool)
{
    strcpy(m_realFile, m_docRoot);
    int length = strlen(m_docRoot);
    const char *p = strrchr(m_url, '/');

    // Handle CGI
    if (m_isCgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        char flag = m_url[1];
        char *tempUrl = (char *)malloc(sizeof(char) * 200);
        strcpy(tempUrl, "/");
        strcat(tempUrl, m_url + 2);
        strncpy(m_realFile + length, tempUrl, MAX_FILENAME_LENGTH - length - 1);
        free(tempUrl);

        // Extract username and password
        char username[100], password[100];
        int i;
        for (i = 5; m_requestData[i] != '&'; ++i)
            username[i - 5] = m_requestData[i];
        username[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_requestData[i] != '\0'; ++i, ++j)
            password[j] = m_requestData[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            // Register
            char *sqlInsert = (char *)malloc(sizeof(char) * 200);
            strcpy(sqlInsert, "INSERT INTO user(username, passwd) VALUES('");
            strcat(sqlInsert, username);
            strcat(sqlInsert, "', '");
            strcat(sqlInsert, password);
            strcat(sqlInsert, "')");

            if (m_users.find(username) == m_users.end())
            {
                m_lock.lock();
                MYSQL *mysql = nullptr;
                ConnectionRAII mysqlConn(&mysql, connPool);

                if (!mysql) std::cout << "None\n";
                int result = mysql_query(mysql, sqlInsert);
                m_users.insert(std::pair<std::string, std::string>(username, password));
                m_lock.unlock();

                if (!result)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");

            free(sqlInsert);
        }
        else if (*(p + 1) == '2')
        {
            // Login
            if (m_users.find(username) != m_users.end() && m_users[username] == password)
                strcpy(m_url, "/menu.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // Handle different URLs
    switch (*(p + 1))
    {
        case '0':
            concatUrl(strlen(m_realFile), "/register.html");
            break;
        case '1':
            concatUrl(strlen(m_realFile), "/log.html");
            break;
        case '4':
            concatUrl(strlen(m_realFile), "/menu.html");
            break;
        case '5':
            concatUrl(strlen(m_realFile), "/picture.html");
            break;
        case '6':
            concatUrl(strlen(m_realFile), "/video.html");
            break;
        case '7':
            concatUrl(strlen(m_realFile), "/about.html");
            break;
        default:
            strncpy(m_realFile + length, m_url, MAX_FILENAME_LENGTH - length - 1);
            break;
    }

    if (stat(m_realFile, &m_fileStat) < 0)
        return NO_RESOURCE;

    if (!(m_fileStat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_fileStat.st_mode))
        return BAD_REQUEST;

    int fd = open(m_realFile, O_RDONLY);
    m_fileAddress = (char *)mmap(0, m_fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void HttpConn::releaseMemory()
{
    if (m_fileAddress)
    {
        munmap(m_fileAddress, m_fileStat.st_size);
        m_fileAddress = nullptr;
    }
}

bool HttpConn::writeToSocket()
{
    int bytes_written = 0;

    if (m_bytesToSend == 0)
    {
        modFd(g_epollFd, m_socketFd, EPOLLIN, m_triggerMode);
        reset();
        return true;
    }

    while (true)
    {
        bytes_written = writev(m_socketFd, m_iov, m_iovCount);

        if (bytes_written < 0)
        {
            if (errno == EAGAIN)
            {
                modFd(g_epollFd, m_socketFd, EPOLLOUT, m_triggerMode);
                return true;
            }
            releaseMemory();
            return false;
        }

        m_bytesHaveSent += bytes_written;
        m_bytesToSend -= bytes_written;
        if (m_bytesHaveSent >= m_iov[0].iov_len)
        {
            m_iov[0].iov_len = 0;
            m_iov[1].iov_base = m_fileAddress + (m_bytesHaveSent - m_writeIndex);
            m_iov[1].iov_len = m_bytesToSend;
        }
        else
        {
            m_iov[0].iov_base = m_writeBuffer + m_bytesHaveSent;
            m_iov[0].iov_len -= m_bytesHaveSent;
        }

        if (m_bytesToSend <= 0)
        {
            releaseMemory();
            modFd(g_epollFd, m_socketFd, EPOLLIN, m_triggerMode);

            if (m_keepAlive)
            {
                reset();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}


bool HttpConn::appendResponse(const char *format, ...)
{
    if (m_writeIndex >= MAX_WRITE_BUFFER_SIZE)
        return false;
    va_list args;
    va_start(args, format);
    int len = vsnprintf(m_writeBuffer + m_writeIndex, MAX_WRITE_BUFFER_SIZE - 1 - m_writeIndex, format, args);
    if (len >= (MAX_WRITE_BUFFER_SIZE - 1 - m_writeIndex))
    {
        va_end(args);
        return false;
    }
    m_writeIndex += len;
    va_end(args);

    LOG_INFO("request:%s", m_writeBuffer);

    return true;
}
bool HttpConn::appendStatusLine(int status, const char *title)
{
    return appendResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool HttpConn::appendHeaders(int contentLength)
{
    return appendContentLength(contentLength) 
        && appendContentType() 
        && appendKeepAlive() 
        && appendBlankLine();
}
bool HttpConn::appendContentLength(int contentLength)
{
    return appendResponse("Content-Length:%d\r\n", contentLength);
}
bool HttpConn::appendContentType()
{
    return appendResponse("Content-Type:%s\r\n", "text/html");
}
bool HttpConn::appendKeepAlive()
{
    return appendResponse("Connection:%s\r\n", (m_keepAlive ? "keep-alive" : "close"));
}
bool HttpConn::appendBlankLine()
{
    return appendResponse("%s", "\r\n");
}
bool HttpConn::appendContent(const char *content)
{
    return appendResponse("%s", content);
}


bool HttpConn::processWrite(HttpCode result)
{
    switch (result)
    {
    case INTERNAL_ERROR:
    {
        appendStatusLine(500, HTTP_STATUS_INTERNAL_ERROR_TITLE);
        appendHeaders(strlen(HTTP_STATUS_INTERNAL_ERROR_MESSAGE));
        if (!appendContent(HTTP_STATUS_INTERNAL_ERROR_MESSAGE))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        appendStatusLine(404, HTTP_STATUS_BAD_REQUEST_TITLE);
        appendHeaders(strlen(HTTP_STATUS_BAD_REQUEST_MESSAGE));
        if (!appendContent(HTTP_STATUS_BAD_REQUEST_MESSAGE))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        appendStatusLine(403, HTTP_STATUS_FORBIDDEN_TITLE);
        appendHeaders(strlen(HTTP_STATUS_FORBIDDEN_MESSAGE));
        if (!appendContent(HTTP_STATUS_FORBIDDEN_MESSAGE))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        appendStatusLine(200, HTTP_STATUS_OK_TITLE);
        if (m_fileStat.st_size != 0)
        {
            appendHeaders(m_fileStat.st_size);
            m_iov[0].iov_base = m_writeBuffer;
            m_iov[0].iov_len = m_writeIndex;
            m_iov[1].iov_base = m_fileAddress;
            m_iov[1].iov_len = m_fileStat.st_size;
            m_iovCount = 2;
            m_bytesToSend = m_writeIndex + m_fileStat.st_size;
            return true;
        }
        else
        {
            const char *emptyPage = "<html><body></body></html>";
            appendHeaders(strlen(emptyPage));
            if (!appendContent(emptyPage))
                return false;
        }
        break;
    }
    default:
        return false;
    }
    m_iov[0].iov_base = m_writeBuffer;
    m_iov[0].iov_len = m_writeIndex;
    m_iovCount = 1;
    m_bytesToSend = m_writeIndex;
    return true;
}
    
void HttpConn::handleRequest(ConnectionPool* connPool)
{
    HttpCode readResult = processRead(connPool);
    if (readResult == NO_REQUEST)
    {
        modFd(g_epollFd, m_socketFd, EPOLLIN, m_triggerMode);
        return;
    }
    bool writeResult = processWrite(readResult);
    if (!writeResult)
    {
        closeConn();
    }
    modFd(g_epollFd, m_socketFd, EPOLLOUT, m_triggerMode);
}

