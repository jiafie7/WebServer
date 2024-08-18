#ifndef HTTPCONN_H
#define HTTPCONN_H
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
#include <map>

#include "../lock/locker.h"
#include "../mysql/connection_pool.h"
#include "../timer/timer_list.h"
#include "../log/log.h"

class HttpConn
{
public:
    static const int MAX_FILENAME_LENGTH = 200;
    static const int MAX_READ_BUFFER_SIZE = 2048;
    static const int MAX_WRITE_BUFFER_SIZE = 1024;

    enum Method
    {
        GET = 0,
        POST,
        PUT,
        DELETE
    };

    enum CheckState
    {
        CHECK_STATE_REQUEST_LINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    
    enum HttpCode
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    enum LineStatus
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    HttpConn() {}
    ~HttpConn() {}

public:
    void init(int socketFd, const sockaddr_in &address, char *, int, int, std::string user, std::string password, std::string databaseName);
    void closeConn(bool realClose = true);
    void handleRequest(ConnectionPool* connPool);
    bool readFromSocket();
    bool writeToSocket();
    sockaddr_in *getAddress()
    {
        return &m_address;
    }
    void initMysqlResult(ConnectionPool *connPool);


private:
    void reset();
    HttpCode processRead(ConnectionPool* connPool);
    bool processWrite(HttpCode result);
    HttpCode parseRequestLine(char *text);
    HttpCode parseHeaders(char *text);
    HttpCode parseContent(char *text);

    void concatUrl(int length, const char* url);
    HttpCode generateRequest(ConnectionPool* connPool);
    char *currentLine() { return m_readBuffer + m_startLine; };
    LineStatus parseLine();

    void releaseMemory();
    bool appendResponse(const char *format, ...);
    bool appendContent(const char *content);
    bool appendStatusLine(int status, const char *title);
    bool appendHeaders(int contentLength);
    bool appendContentType();
    bool appendContentLength(int contentLength);
    bool appendKeepAlive();
    bool appendBlankLine();

public:
    static int g_epollFd;
    static int g_userCount;

    MYSQL *mysql;
    int requestState;  // 0 for read, 1 for write
    int timerFlag;
    int isImproved;

private:
    int m_socketFd;
    sockaddr_in m_address;
    char m_readBuffer[MAX_READ_BUFFER_SIZE];
    long m_readIndex;
    long m_checkedIndex;
    int m_startLine;
    char m_writeBuffer[MAX_WRITE_BUFFER_SIZE];
    int m_writeIndex;

    CheckState m_checkState;
    Method m_method;
    char m_realFile[MAX_FILENAME_LENGTH];
    char *m_url;
    char *m_version;
    char *m_host;
    long m_contentLength;
    bool m_keepAlive;

    char *m_fileAddress; // file content in memory

    struct stat m_fileStat;
    struct iovec m_iov[2];
    int m_iovCount;
    int m_isCgi;            // Indicates if POST is enabled
    char *m_requestData; // Stores request content data
    int m_bytesToSend;
    int m_bytesHaveSent;
    char *m_docRoot;

    int m_triggerMode;
    int m_logStatus;

    char m_user[100];
    char m_password[100];
    char m_databaseName[100];

    // Locker m_lock;
    // std::map<std::string, std::string> m_users;
};

#endif
