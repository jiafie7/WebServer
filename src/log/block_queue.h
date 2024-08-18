/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template <class T>
class BlockQueue
{
public:
    explicit BlockQueue(int maxSize = 1000)
    {
        if (maxSize <= 0)
        {
            exit(-1);
        }

        m_maxSize = maxSize;
        m_array = new T[maxSize];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~BlockQueue()
    {
        m_mutex.lock();
        if (m_array != nullptr) {
            delete[] m_array;
        }
        m_mutex.unlock();
    }

    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    bool isFull()
    {
        m_mutex.lock();
        bool isFull = (m_size >= m_maxSize);
        m_mutex.unlock();
        return isFull;
    }

    bool isEmpty()
    {
        m_mutex.lock();
        bool isEmpty = (m_size == 0);
        m_mutex.unlock();
        return isEmpty;
    }

    bool front(T &value)
    {
        m_mutex.lock();
        if (m_size == 0)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    
    bool back(T &value)
    {
        m_mutex.lock();
        if (m_size == 0)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int getSize()
    {
        m_mutex.lock();
        int size = m_size;
        m_mutex.unlock();
        return size;
    }

    int getMaxSize()
    {
        m_mutex.lock();
        int maxSize = m_maxSize;
        m_mutex.unlock();
        return maxSize;
    }

    // 往队列添加元素，需要将所有使用队列的线程先唤醒
    // 当有元素push进队列，相当于生产者生产了一个元素
    bool push(const T &item)
    {
        m_mutex.lock();
        if (m_size >= m_maxSize)
        {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_maxSize;
        m_array[m_back] = item;
        ++ m_size;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    // pop时，如果当前队列没有元素，将会等待条件变量
    bool pop(T &item)
    {
        m_mutex.lock();
        while (m_size <= 0)
        {
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_maxSize;
        item = m_array[m_front];
        -- m_size;
        m_mutex.unlock();
        return true;
    }

    // 增加了超时处理
    bool pop(T &item, int msTimeout)
    {
        struct timespec timeoutSpec = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);

        m_mutex.lock();
        if (m_size <= 0)
        {
            timeoutSpec.tv_sec = now.tv_sec + msTimeout / 1000;
            timeoutSpec.tv_nsec = (msTimeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), timeoutSpec))
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_maxSize;
        item = m_array[m_front];
        -- m_size;
        m_mutex.unlock();
        return true;
    }


private:
    Locker m_mutex;
    CondVar m_cond;

    T *m_array;
    int m_maxSize;
    int m_size;
    int m_front;
    int m_back;
};

#endif
