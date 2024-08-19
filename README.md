# WebServer

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
