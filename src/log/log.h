#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

class Log
{
public:
    //C++11以后,使用局部变量懒汉不用加锁
    static Log *getInstance()
    {
        static Log instance;
        return &instance;
    }

    static void *flushLogThread(void *args)
    {
        Log::getInstance()->asyncWriteLog();
        return nullptr;
    }

    // 初始化日志文件、日志缓冲区大小、最大行数、日志队列最大长度
    bool init(const char *fileName, int logStatus, int logBufSize = 8192, int splitLines = 5000000, int maxQueueSize = 0);

    void writeLog(int level, const char *format, ...);

    void flush();

private:
    Log();
    virtual ~Log();
    void *asyncWriteLog()
    {
        std::string singleLog;

        //从阻塞队列中取出一个日志string，写入文件
        while (m_logQueue->pop(singleLog))
        {
            m_mutex.lock();
            fputs(singleLog.c_str(), m_fp);
            m_mutex.unlock();
        }
        return nullptr;
    }

private:
    char m_dirName[128];            // 路径名
    char m_logName[128];            // 日志文件名
    int m_splitLines;               // 日志最大行数
    int m_logBufSize;               // 日志缓冲区大小
    long long m_logCount;           // 日志行数记录
    int m_currentDay;               // 当前日期（用于按天分类）
    FILE *m_fp;                     // 日志文件指针
    char *m_buffer;
    BlockQueue<std::string> *m_logQueue; // 阻塞队列
    bool m_isAsync;                 // 是否异步标志
    Locker m_mutex;
    int m_logStatus;                 // 日志状态
};

#define LOG_DEBUG(logStatus, format, ...) if(0 == logStatus) {Log::getInstance()->writeLog(0, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_INFO(logStatus, format, ...) if(0 == logStatus) {Log::getInstance()->writeLog(1, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_WARN(logStatus, format, ...) if(0 == logStatus) {Log::getInstance()->writeLog(2, format, ##__VA_ARGS__); Log::getInstance()->flush();}
#define LOG_ERROR(logStatus, format, ...) if(0 == logStatus) {Log::getInstance()->writeLog(3, format, ##__VA_ARGS__); Log::getInstance()->flush();}

#endif
