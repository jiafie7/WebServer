#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>

Log::Log()
    : m_logCount(0)
    , m_isAsync(false)
    , m_fp(nullptr)
    , m_buffer(nullptr)
    , m_logQueue(nullptr)
{
}

Log::~Log()
{
    if (m_fp != nullptr)
    {
        fclose(m_fp);
    }
    if (m_buffer != nullptr)
    {
        delete[] m_buffer;
    }
    if (m_logQueue != nullptr)
    {
        delete m_logQueue;
    }
}

bool Log::init(const char *fileName, int logStatus, int logBufSize, int splitLines, int maxQueueSize)
{
    if (maxQueueSize >= 1)
    {
        m_isAsync = true;
        m_logQueue = new BlockQueue<std::string>(maxQueueSize);
        pthread_t tid;
        pthread_create(&tid, nullptr, flushLogThread, nullptr);
    }
    
    m_logStatus = logStatus;
    m_logBufSize = logBufSize;
    m_buffer = new char[m_logBufSize];
    memset(m_buffer, '\0', m_logBufSize);
    m_splitLines = splitLines;

    time_t t = time(nullptr);
    struct tm *sysTm = localtime(&t);
    struct tm myTm = *sysTm;

    const char *p = strrchr(fileName, '/');
    char logFullName[512] = {0};

    if (p == nullptr)
    {
        snprintf(logFullName, sizeof(logFullName) - 1, "%d_%02d_%02d_%s",
                 myTm.tm_year + 1900, myTm.tm_mon + 1, myTm.tm_mday, fileName);
    }
    else
    {
        strncpy(m_logName, p + 1, sizeof(m_logName) - 1);
        strncpy(m_dirName, fileName, p - fileName + 1);
        snprintf(logFullName, sizeof(logFullName) - 1, "%s%d_%02d_%02d_%s",
                 m_dirName, myTm.tm_year + 1900, myTm.tm_mon + 1, myTm.tm_mday, m_logName);
    }

    m_currentDay = myTm.tm_mday;

    m_fp = fopen(logFullName, "a");
    if (m_fp == nullptr)
    {
        return false;
    }

    return true;
}

void Log::writeLog(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm *sysTm = localtime(&t);
    struct tm myTm = *sysTm;
    char levelStr[16] = {0};

    switch (level)
    {
    case 0:
        strcpy(levelStr, "[debug]:");
        break;
    case 1:
        strcpy(levelStr, "[info]:");
        break;
    case 2:
        strcpy(levelStr, "[warn]:");
        break;
    case 3:
        strcpy(levelStr, "[error]:");
        break;
    default:
        strcpy(levelStr, "[info]:");
        break;
    }

    m_mutex.lock();
    m_logCount++;

    if (m_currentDay != myTm.tm_mday || m_logCount % m_splitLines == 0)
    {
        char newLogFileName[512] = {0};
        fflush(m_fp);
        fclose(m_fp);

        char tail[16] = {0};
        snprintf(tail, sizeof(tail), "%d_%02d_%02d_", myTm.tm_year + 1900, myTm.tm_mon + 1, myTm.tm_mday);

        if (m_currentDay != myTm.tm_mday)
        {
            snprintf(newLogFileName, sizeof(newLogFileName) - 1, "%s%s%s", m_dirName, tail, m_logName);
            m_currentDay = myTm.tm_mday;
            m_logCount = 0;
        }
        else
        {
            snprintf(newLogFileName, sizeof(newLogFileName) - 1, "%s%s%s.%lld", m_dirName, tail, m_logName, m_logCount / m_splitLines);
        }
        m_fp = fopen(newLogFileName, "a");
    }

    m_mutex.unlock();

    va_list args;
    va_start(args, format);

    std::string logMessage;
    m_mutex.lock();

    int prefixLen = snprintf(m_buffer, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                             myTm.tm_year + 1900, myTm.tm_mon + 1, myTm.tm_mday,
                             myTm.tm_hour, myTm.tm_min, myTm.tm_sec, now.tv_usec, levelStr);

    int messageLen = vsnprintf(m_buffer + prefixLen, m_logBufSize - prefixLen - 1, format, args);
    m_buffer[prefixLen + messageLen] = '\n';
    m_buffer[prefixLen + messageLen + 1] = '\0';
    logMessage = m_buffer;

    m_mutex.unlock();

    if (m_isAsync && !m_logQueue->isFull())
    {
        m_logQueue->push(logMessage);
    }
    else
    {
        m_mutex.lock();
        fputs(logMessage.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(args);
}

void Log::flush()
{
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}
