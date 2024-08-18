#include "src/config/config.h"
#include "src/webserver/webserver.h"

int main(int argc, char *argv[])
{
    // Database connection information
    std::string username = "USER";
    std::string password = "PASSWORD";
    std::string databaseName = "DATABASE";

    // Command line argument parsing
    Config config;
    config.parseArguments(argc, argv);

    WebServer server;

    // Initialize the server
    server.init(config.port, username, password, databaseName, config.logWriteMethod, 
                config.enableLinger, config.triggerMode, config.sqlConnectionPoolSize, 
                config.threadPoolSize, config.logStatus, config.actorModel);


    // Setup logging
    server.setupLogging();

    // Setup database connection pool
    server.setupDatabaseConnectionPool();

    // Setup thread pool
    server.setupThreadPool();

    // Configure trigger mode
    server.configureTriggerMode();

    // Start listening for events
    server.startListening();

    // Enter the event loop
    server.startEventLoop();

    return 0;
}
