#ifndef CONFIG_H
#define CONFIG_H

#include "../webserver/webserver.h"

class Config
{
public:
    Config();
    ~Config() = default;

    void parseArguments(int argc, char* argv[]);

    // Port number
    int port;

    // Log writing method
    int logWriteMethod;

    // Trigger mode combination
    int triggerMode;

    // Listen file descriptor trigger mode
    int listenTriggerMode;

    // Connection file descriptor trigger mode
    int connectionTriggerMode;

    // Graceful connection closing
    int enableLinger;

    // Number of database connections in the pool
    int sqlConnectionPoolSize;

    // Number of threads in the thread pool
    int threadPoolSize;

    // Log status
    int logStatus;

    // Concurrency model selection
    int actorModel;
};

#endif
