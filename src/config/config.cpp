#include "config.h"

Config::Config()
    : port(7777),
      logWriteMethod(0),         // Log writing mode, synchronous by default
      triggerMode(0),            // Default listenfd LT + connfd LT
      listenTriggerMode(0),      // Default LT
      connectionTriggerMode(0),  // Default LT
      enableLinger(0),           // Not used by default
      sqlConnectionPoolSize(8),
      threadPoolSize(8),
      logStatus(0),              // Logging is enabled by default
      actorModel(0)              // Default proactor
{
}

void Config::parseArguments(int argc, char* argv[])
{
    int option;
    const char *optionString = "p:l:m:o:s:t:c:a:";
    while ((option = getopt(argc, argv, optionString)) != -1)
    {
        switch (option)
        {
        case 'p':
            port = std::atoi(optarg);
            break;
        case 'l':
            logWriteMethod = std::atoi(optarg);
            break;
        case 'm':
            triggerMode = std::atoi(optarg);
            break;
        case 'o':
            enableLinger = std::atoi(optarg);
            break;
        case 's':
            sqlConnectionPoolSize = std::atoi(optarg);
            break;
        case 't':
            threadPoolSize = std::atoi(optarg);
            break;
        case 'c':
            logStatus = std::atoi(optarg);
            break;
        case 'a':
            actorModel = std::atoi(optarg);
            break;
        default:
            break;
        }
    }
}