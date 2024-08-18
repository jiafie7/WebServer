#ifndef CONNECTION_POOL
#define CONNECTION_POOL

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

class ConnectionPool
{
public:
	MYSQL *getConnection();                   // 获取数据库连接
    bool releaseConnection(MYSQL *conn);      // 释放连接
    int getFreeConn() const;             // 获取空闲连接数
    void destroyPool();                       // 销毁所有连接

	//单例模式
	static ConnectionPool *getInstance();

 	void init(const std::string &url, const std::string &user, const std::string &password, 
        const std::string &databaseName, int port, int maxConn, int logStatus);
	
private:
	ConnectionPool();
	~ConnectionPool();

	int m_maxConn;       // 最大连接数
    int m_curConn;       // 当前已使用的连接数
    int m_freeConn;      // 当前空闲的连接数
	Locker m_lock;
    list<MYSQL *> m_connList; // 连接池
    Semaphore m_reserve;

public:
	std::string m_url;             // 主机地址
    int m_port;               // 数据库端口号
    std::string m_user;            // 数据库用户名
    std::string m_password;        // 数据库密码
    std::string m_databaseName;    // 数据库名称
    int m_logStatus;          // 日志开关
};

class ConnectionRAII{
public:
	ConnectionRAII(MYSQL **conn, ConnectionPool *connPool);
    ~ConnectionRAII();
	
private:
    MYSQL **m_sql;
    MYSQL *m_connRAII;
    ConnectionPool *m_poolRAII;
};

#endif
