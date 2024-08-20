#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "connection_pool.h"


ConnectionPool::ConnectionPool() 
	: m_curConn(0), m_freeConn(0)
{}

ConnectionPool *ConnectionPool::getInstance() {
    static ConnectionPool connPool;
    return &connPool;
}

void ConnectionPool::init(const std::string &url, const std::string &user, const std::string &password, 
    const std::string &dbName, int port, int maxConn, int logStatus)
{
	m_url = url;
    m_port = port;
    m_user = user;
    m_password = password;
    m_databaseName = dbName;
    m_logStatus = logStatus;

	for (int i = 0; i < maxConn; ++i) {
        MYSQL *conn = mysql_init(nullptr);

        if (conn == nullptr) {
            LOG_ERROR(m_logStatus, "MySQL initialization error");
            exit(1);
        }
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), password.c_str(), 
                                  dbName.c_str(), port, nullptr, 0);

        if (conn == nullptr) {
            LOG_ERROR(m_logStatus, "MySQL connection error");
            exit(1);
        }
        m_connList.push_back(conn);
        ++m_freeConn;
    }

	m_reserve = Semaphore(m_freeConn);
    m_maxConn = m_freeConn;
}


// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *ConnectionPool::getConnection()
{
	if (m_connList.empty())
        return nullptr;

	m_reserve.wait();
    m_lock.lock();

	MYSQL *conn = m_connList.front();
    m_connList.pop_front();

    --m_freeConn;
    ++m_curConn;

    m_lock.unlock();
    return conn;
}

// 释放当前使用的连接
bool ConnectionPool::releaseConnection(MYSQL *conn)
{
	if (conn == nullptr)
        return false;

	m_lock.lock();

	m_connList.push_back(conn);
    ++m_freeConn;
    --m_curConn;

    m_lock.unlock();

    m_reserve.post();
    return true;
}

// 销毁数据库连接池
void ConnectionPool::destroyPool()
{
	m_lock.lock();
    if (!m_connList.empty()) {
        for (MYSQL *conn : m_connList) {
            mysql_close(conn);
        }
        m_curConn = 0;
        m_freeConn = 0;
        m_connList.clear();
    }
    m_lock.unlock();
}

// 当前空闲的连接数
int ConnectionPool::getFreeConn() const
{
    return m_freeConn;
}

ConnectionPool::~ConnectionPool()
{
    destroyPool();
}


ConnectionRAII::ConnectionRAII(MYSQL **sql, ConnectionPool *connPool) : m_sql(sql), m_poolRAII(connPool) {
        *m_sql = m_poolRAII->getConnection();
        m_connRAII = *m_sql;
    }

 ConnectionRAII::~ConnectionRAII() {
        m_poolRAII->releaseConnection(m_connRAII);
        *m_sql = NULL; 
    }
