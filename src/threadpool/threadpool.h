#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../mysql/connection_pool.h"

template <typename T>
class ThreadPool
{
public:
    ThreadPool(int actorModel, ConnectionPool *connPool, int threadNumber = 8, int maxRequests = 10000);
    ~ThreadPool();
    bool append(T *request, int state);
    bool appendP(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_threadNumber;          // 线程池中的线程数
    int m_maxRequests;           // 请求队列中允许的最大请求数
    pthread_t *m_threads;        // 描述线程池的数组，其大小为m_threadNumber
    std::list<T *> m_workQueue;  // 请求队列
    Locker m_queueLocker;        // 保护请求队列的互斥锁
    Semaphore m_queueStat;       // 是否有任务需要处理
    ConnectionPool *m_connPool;  // 数据库连接池
    int m_actorModel;            // 事件处理模式
};

template <typename T>
ThreadPool<T>::ThreadPool(int actorModel, ConnectionPool *connPool, int threadNumber, int maxRequests)
    : m_actorModel(actorModel)
    , m_threadNumber(threadNumber)
    , m_maxRequests(maxRequests)
    , m_threads(nullptr)
    , m_connPool(connPool)
{
    if (threadNumber <= 0 || maxRequests <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_threadNumber];
    if (!m_threads)
        throw std::exception();

    for (int i = 0; i < threadNumber; ++i)
    {
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
    delete[] m_threads;
}

template <typename T>
bool ThreadPool<T>::append(T *request, int state)
{
    m_queueLocker.lock();
    if (m_workQueue.size() >= m_maxRequests)
    {
        m_queueLocker.unlock();
        return false;
    }
    request->requestState = state;
    m_workQueue.push_back(request);
    m_queueLocker.unlock();
    m_queueStat.post();
    return true;
}

template <typename T>
bool ThreadPool<T>::appendP(T *request)
{
    m_queueLocker.lock();
    if (m_workQueue.size() >= m_maxRequests)
    {
        m_queueLocker.unlock();
        return false;
    }
    m_workQueue.push_front(request); // Insert at the front of the queue
    m_queueLocker.unlock();
    m_queueStat.post();
    return true;
}

template <typename T>
void *ThreadPool<T>::worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void ThreadPool<T>::run()
{
    while (true)
    {
        m_queueStat.wait();
        m_queueLocker.lock();
        if (m_workQueue.empty())
        {
            m_queueLocker.unlock();
            continue;
        }

        T *request = m_workQueue.front();
        m_workQueue.pop_front();
        m_queueLocker.unlock();
        if (!request)
            continue;
        // Process the request
        if (m_actorModel == 1)
        {
            if (request->requestState == 0)
            {
                if (request->readFromSocket())
                {
                  request->isImproved = 1;
                  ConnectionRAII mysqlconn(&request->mysql, m_connPool);
                  request->handleRequest(m_connPool);
                }
                else
                {
                  request->isImproved = 1
                  request->timerFlag = 1;
                }
            }
            else
            {
                if (request->writeToSocket())
                {
                  request->isImproved = 1
                }
                else
                {
                  request->isImproved = 1
                  request->timerFlag = 1;
                }
            }
        }
        else
        {
            ConnectionRAII mysqlconn(&request->mysql, m_connPool);
            request->handleRequest(m_connPool);
        }
    }
}
#endif
