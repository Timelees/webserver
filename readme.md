
## 运行
1. MySQL数据库

```
// 建立yourdb库
create database yourdb;

// 创建user表
USE yourdb;
CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;

// 添加数据
INSERT INTO user(username, passwd) VALUES('name', 'passwd');
```

2. 修改config.hpp中的数据库初始化信息

```
// 数据库配置参数
std::string db_user_ = "lee";
std::string db_password_ = "123";
std::string db_name_ = "webserverDB";
```

3. 构建
```
mkdir build && cd build
cmake ..
make -j 6
```

4. 服务器运行
```
./webserver
```

5. 浏览器访问
```
http://localhost:8080/
```

6. 参数化运行
```
./webserver [-p port] [-l linger_mode] [-t trig_mode] [-a actor_mode] [-m concurrent_mode] [-c close_log] [-w log_write] [-s sql_num] [-x thread_num]
```
**示例：**
```
./webserver -p 8080 -l 0 -t 0 -a 1 -m 0 -c 0 -w 0 -s 8 -x 8
```

7. 参数说明
- -p ：设置服务器端口号
    - 默认8888

- -l : 连接关闭模式，默认0
    - 0：关闭连接时不延迟
    - 1：延迟关闭连接

- -t : listenfd和connfd的模式组合，默认0，使用LT + LT
    - 0：LT + LT
    - 1：LT + ET
    - 2：ET + LT
    - 3：ET + ET

- -a : 事件反应堆模型，默认1，Proactor 
    - 0：Reactor
    - 1：Proactor

- -m ：并发模型，默认0，半同步/半异步模型
    - 0：半同步/半异步模型
    - 1: 领导者/跟随者模型

- -c : 日志是否关闭，默认0，不关闭
    - 0：不关闭
    - 1：关闭

- -w ：日志写入方式，默认0，同步写入
    - 0：同步写入
    - 1：异步写入

- -s : 数据库连接池数量
    - 默认8

- -x : 线程池数量
    - 默认8 