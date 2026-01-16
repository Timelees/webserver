# WebSever服务器

## 需求：

### 服务器实现

http请求实现

http请求解析

http请求响应

线程池处理

数据库数据处理



### http处理

解析客户端的请求信息，后端实现对应的处理逻辑



### 线程池实现

服务器高并发处理数据



### 命令行参数解析

自定义端口以及线程池大小等信息



### 数据库管理

管理用户相关数据

==（暂定存储不同用户的登录日志？）==



### 压力测试

测试服务器稳定性



## 实现：

### 服务器功能实现——webserver.cpp

#### 初始化——init()

初始化以下内容，数据应从配置文件进行加载读取(==当前未实现配置文件的读取==)

| 变量         | 含义                     |
| ------------ | ------------------------ |
| port_        | 端口号                   |
| linger_mode_ | close行为模式            |
| trig_mode_   | epoll的触发模式（ET/LT） |
|              |                          |
|              |                          |

#### 事件监听初始化——eventListen()

初始化服务器监听的ip地址和端口号，创建epoll的内核事件表。

**流程：**

1. 创建监听套接字（IPV4）

	```c++
	listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
	```

	AF_INET对应IPV4协议族，SOCK_STREAM对应服务流服务（TCP协议）。如果要用UDP的话，就是SOCK_UGRAM。

	==TODO：是否可以实现同时支持IPV4和IPV6、TCP和UDP的服务器==

2. socket选项设置（端口复用，close行为设置）

	**端口复用：**

	```C++
	int opt = 1;
	    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));    // 允许端口复用
	```

	SO_REUSEADDR可以强制使用被处于TIME_WAIT状态的连接占用的socket地址。

	==TODO：TCP连接的TIME_WAIT状态==

	**close行为设置：**

	SO_LINGER选项用于控制close系统调用在关闭TCP连接时的行为。

	与其设置相关的是linger类型的结构体

	```c++
	#include＜sys/socket.h＞
	struct linger
	{
	int l_onoff;/*开启（非0）还是关闭（0）该选项*/
	int l_linger;/*滞留时间*/
	};
	```

	默认情况下，当我们使用close系统调用来关闭一个socket时，close将立即返回，TCP模块负责把该socket对应的TCP发送缓冲区中残留的数据发送给对方。

	- 此时，l_onoff等于0。此时SO_LINGER选项不起作用，close用默认行为来关闭socket。

		```c++
		if(0 == linger_mode_){
		        // close默认行为，close将立即返回，TCP模块负责把该socket对应的TCP发送缓冲区中残留的数据发送给对方。
		        so_linger.l_onoff = 0;  // 关闭时不等待数据发送完毕
		        so_linger.l_linger = 0; // 滞留时间为0
		        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
		    }
		```

		

	- l_onoff不为0，l_linger等于0。此时close系统调用立即返回，TCP模块将丢弃被关闭的socket对应的TCP发送缓冲区中残留的数据，同时给对方发送一个复位报文段。因此，这种情况给服务器提供了异常终止一个连接的方法(==TODO: 这种关闭模式一般在什么情况下使用？==)。

		```c++
		else if(1 == linger_mode_){
		        //  close系统调用立即返回，TCP模块将丢弃被关闭的socket对应的TCP发送缓冲区中残留的数据，同时给对方发送一个复位报文段
		        so_linger.l_onoff = 1; 
		        so_linger.l_linger = 0; 
		        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
		    }
		```

		

	- l_onoff不为0，l_linger大于0。此时close的行为取决于两个条件：一是被关闭的socket对应的TCP发送缓冲区中是否还有残留的数据；二是该socket是阻塞的，还是非阻塞的。对于阻塞的socket，close将等待一段长为l_linger的时间，直到TCP模块发送完所有残留数据并得到对方的确认。如果这段时间内TCP模块没有发送完残留数据并得到对方的确认，那么close系统调用将返回-1并设置errno为EWOULDBLOCK。如果socket是非阻塞的，close将立即返回，此时我们需要根据其返回值和errno来判断残留数据是否已经发送完毕。

		```c++
		else{
		        // close的行为取决于两个条件：一是被关闭的socket对应的TCP发送缓冲区中是否还有残留的数据；二是该socket是阻塞的，还是非阻塞的。
		        so_linger.l_onoff = 1;  // 关闭时不等待数据发送完毕
		        so_linger.l_linger = 1; // 滞留时间为0
		        setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
		    }
		```

		

3. 设置监听ip地址和端口号

	socket经典操作

	```c++
	// 设置监听ip地址和端口号
	    struct sockaddr_in server_addr;
	    bzero(&server_addr, sizeof(server_addr));
	    server_addr.sin_family = AF_INET;  // IPV4地址
	    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);    // 绑定到任意IP地址
	    server_addr.sin_port = htons(port_);  // 端口号转换为网络字节序
	```

	==TODO：实现IPV6的时候需要修改这里？？？==

	

4. 绑定监听套接字

	将服务器的socket地址分配给监听套接字listen_fd_，将其绑定后相当于给监听套接字命名，才能使客户端知道如何连接服务端。

	```c++
	int ret = bind(listen_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
	```

	

5. 设置监听

	设置一个监听队列，存放需要处理的客户端连接。第二个参数表示内核监听队列的最大长度。监听队列的长度如果超过backlog，服务器将不受理新的客户连接，客户端也将收到ECONNREFUSED错误信息。

	==TODO：监听队列长度是否意思是超过5个客户端连接后就不能连接了？==

	```c++
	ret = listen(listen_fd_, 5);
	```

	

6. epoll内核事件表创建

	把用户关心的文件描述符上的事件放在内核的一个事件表中。通过ep_fd_指向这个内核表，后面有建立成功的连接就添加到内核事件表上。

	```c++
	ep_fd_ = epoll_create(1024);
	```

	

7. 向内核事件表注册监听套接字

	封装util函数中的utilEpoll类，实现epoll相关的操作。

	```c++
	 util_epoll_.addFd(ep_fd_, listen_fd_, false, trig_mode_);
	```



### http功能实现

#### http状态定义——http_message.hpp

**http请求方法：**

```
    enum METHOD{
        GET = 0,    // 申请获取资源
        HEAD,       // 仅获取资源的响应头
        POST,       // 提交数据给服务器
        PUT,        // 上传资源，客户端向服务器传送的数据取代指定的文档内容
        DELETE,     // 删除指定的资源
        TRACE,      // 返回原始HTTP请求的内容，主要用于测试或诊断
        OPTIONS,    // 返回服务器针对特定资源所支持的HTTP请求方法
        CONNECT,    // 将请求连接转换为透明的TCP/IP隧道，通常用于SSL加密的连接
        PATCH       // 对资源进行部分修改
    };
```

**http解析状态机标识：**

【通过状态机来判断当前分析的内容，调用对应的解析函数】

```
   enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0, // 当前正在分析请求行
        CHECK_STATE_HEADER,           // 当前正在分析头部字段
        CHECK_STATE_CONTENT           // 当前正在解析请求体
    };
```

**http请求返回状态：**

```
    enum HTTP_CODE{
        NO_REQUEST,         // 请求不完整，需要继续读取客户数据
        GET_REQUEST,        // 获得了完整的HTTP请求
        BAD_REQUEST,        // HTTP请求有语法错误
        NO_RESOURCE,        // 请求资源不存在
        FORBIDDEN_REQUEST,  // 客户对资源没有足够的访问权限
        FILE_REQUEST,       // 文件请求，获取文件成功
        INTERNAL_ERROR,     // 服务器内部错误
        CLOSED_CONNECTION   // 客户端已经关闭连接
    };
```

**解析行状态：**

【对http请求内容进行解析首行和协议头时，将其分割成一行一行来进行解析，通过此状态来判断是否提取出完整的行】

```
    enum LINE_STATUS{
        LINE_OK = 0,    // 读取到一个完整的行
        LINE_BAD,       // 行出错
        LINE_OPEN       // 行数据尚且不完整
    };
```



#### http请求——http_request.cpp

![image-20220303222646809](https://gitee.com/Leesj008/typora-image/raw/master/images/aa818e252211c6a787de3f30008f37b3.png)

##### **逐行提取请求报文**

```c
http_message::LINE_STATUS http_request::parse_line()
```

使用checked_idx\_指向当前正在解析的位置，read_idx\_表示读缓冲区read_buf\_的末尾。遍历增加check_idx\_。

对于如下请求报文，每一行的末尾都是回车符和换行符，仅当read_buf\_的连续两个字符依次为'\r'和'\n'时，表示为报文请求行和请求头的一行，返回LINE_OK：

```
POST /test HTTP/1.1
Host: localhost:8888
User-Agent: curl/7.81.0
Accept: */*
Content-Type: application/json
Content-Length: 23

{"name":"lee","age":18}
```

![img](https://gitee.com/Leesj008/typora-image/raw/master/images/fd4e859dfcd055cb25eadcdcfcd520fe.png)



##### 解析http请求

```c
http_message::HTTP_CODE http_request::parse_request()
```

通过状态机标识，进行解析请求行【parse_request_line(text)】、解析请求头【parse_request_headers(text)】、解析请求体【parse_request_content(text)】的转换。

请求头解析完成且请求体内容为空  **或**  请求体不为空且解析完成，进行请求响应do_request()



###### 解析http请求行

获取请求方法、请求资源路径url、HTTP版本号

```
POST /test HTTP/1.1
```

使用strpbrk(text, " \t")查找第一个空格或制表符，并将该位置修改为'\0'，从而text就变为"POST\0/test HTTP/1.1",如此可以通过text解析出请求方法为POST

```c++
    url_ = strpbrk(text, " \t"); // 查找第一个空格或制表符
    *url_++ = '\0'; // 将空格或制表符置为\0,并将url_指向下一个字符，此时text指向请求方法（text = "GET"）
```

而将url_指针进行后移便指向"/test HTTP/1.1"，通过strspn(url_, " \t")跳过空格或制表符，解析url。通过strpbrk（）将指针后移跳过空格或制表符，获取版本号指向version_

```c
    // 解析HTTP版本号
    url_ += strspn(url_, " \t"); // 跳过空格或制表符
    version_ = strpbrk(url_, " \t");    // 查找第一个空格或制表符
    *version_++ = '\0'; // 将空格或制表符置为\0,并将version_指向下一个字符, url_指向请求资源路径(/xxx)
    version_ += strspn(version_, " \t"); // 跳过空格或制表符
```



##### 解析请求头

```c
http_message::HTTP_CODE http_request::parse_request_headers(char* text)
```

```
Host: localhost:8888
User-Agent: curl/7.81.0
Accept: */*
Content-Type: application/json
Content-Length: 23
```

主要提取Host、Connection、Content-Length这些内容

请求头有多行，在parse_request()的循环中会不断读取读缓冲区read_buf\_的内容，然后解析键值对。

当传入的text为空行时，表示请求头解析完成。

若请求体的长度为0，表示已经获取到完整的请求，并返回GET_REQUEST的状态码；若不为0，需要切换状态机到CHECK_STATE_CONTENT，并返回NO_REQUEST的状态码，表示需要继续进行解析。



##### 解析请求体

```c
http_message::HTTP_CODE http_request::parse_request_content(char* text)
```

将请求体内容存入content_中。





### 相关工具实现——util.cpp

#### utilEpoll类

##### epoll注册事件

```c++
void utilEpoll::addFd(int epoll_fd, int fd, bool one_shot, int trig_mode)
```

根据one_shot参数决定是否设置权限EPOLLONESHOT，这个权限可以保证一个socket连接在任一时刻只被一个线程处理，避免了多个线程同时操作一个socket的数据的情况。

通过trig_mode参数设置对应的触发模式，ET还是LT。ET模式的效率更加高。

##### epoll删除事件

```c++
void utilEpoll::deleteFd(int epoll_fd, int fd)
```

从内核事件表上取下注册的事件。

