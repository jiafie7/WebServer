# WebServer

<img src = "./presentation.gif" width = "800px" />

A lightweight web server built with C++.

- Thread pool + non-blocking socket + epoll (ET/LT) + event processing (Reactor/simulated Proactor)

- Master-slave state machine parses HTTP request message, supports parsing GET and POST requests

- User registration, login function, request image and video files

- Synchronous/asynchronous log system, records server operation status

## Getting Started

**Step 1:** Create a database and a table.

```
CREATE DATABASE DATABASE_NAME;

USE DATABASE_NAME;

CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;
```

**Step 2:** Modify the database initialization information in `main.cpp`.

```c
std::string username = "USER";
std::string password = "PASSWORD";
std::string databaseName = "DATABASE_NAME";
```

**Step 3:** Build and compile project files.

```sh
mkdir build && cd build
cmake ..
make
```

**Step 4:** Run the WebServer.

```sh
./WebServer
```
