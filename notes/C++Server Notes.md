# C++ Server

## 什么是 Socket

### 1. Socket 套接字

对外提供一个网络通信接口，在 Linux 系统中这个套接字是一个文件描述符，也就是一个 `int` 类型的值

### 2. 两种 Internet Socket

目前只需要认识两种 Socket：

| Socket 类型   | 常用协议 | 特点                               |
| ------------- | -------- | ---------------------------------- |
| `SOCK_STREAM` | TCP      | 可靠、有序、面向连接               |
| `SOCK_DGRAM`  | UDP      | 不保证到达、不保证顺序、通常无连接 |

### 3. Stream Socket：TCP

`SOCK_STREAM` 是串流式 Socket，通常使用 TCP。

**特点：**

- 数据能够可靠传输；
- 数据按照发送顺序到达；
- 通信双方需要先建立连接；
- TCP 负责处理数据丢失、重传和顺序问题。

可以把它理解成一条建立好的“数据管道”：

```text
Client ←──── TCP Connection ────→ Server
```

我们目前写 C++ Server 使用的就是：

```cpp
socket(AF_INET, SOCK_STREAM, 0);
```

**特性：面向连接**

- 一条 TCP 协议由四元组标识

```text
源 IP
源端口
目的 IP
目的端口
```

> TCP 面向连接 = 通信双方先建立一条连接，之后所有数据传输都围绕这条连接进行。

### 4. Datagram Socket：UDP

`SOCK_DGRAM` 是数据报式 Socket，通常使用 UDP。

特点：

- 通常不需要提前建立连接；
- 数据包可能丢失；
- 数据包可能不按发送顺序到达；
- UDP 本身不负责重传；
- 相比 TCP 更强调速度。

可以把每个数据包理解成一封独立发送的信：

```text
Packet 1 ───→
Packet 2 ─────────→ Server
Packet 3 ──X 可能丢失
```

## 文件描述符 fd

Linux 会用一个整数来标识进程打开的资源，这个整数叫文件描述符（File Descriptor，fd）。

```cpp
int sockfd = socket(...);
```

`socket()` 成功后返回一个 fd，例如：

```text
sockfd = 3
    ↓
Linux 内核中的某个 Socket
```

后续通过这个 fd 操作 Socket：

```cpp
bind(sockfd, ...);
listen(sockfd, ...);
```

## `sockaddr_in`

`sockaddr_in` 用于表示 IPv4 地址。

```cpp
sockaddr_in serv_addr{};
```

当前主要关注：

```cpp
serv_addr.sin_family   // IPv4
serv_addr.sin_addr     // IP
serv_addr.sin_port     // Port
```

例如：

```cpp
serv_addr.sin_family = AF_INET;
serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
serv_addr.sin_port = htons(8888);
```

表示：

```text
IPv4
127.0.0.1
8888
```

---

## `htons()`

网络通信需要使用统一的网络字节序。

```cpp
htons(8888);
```

表示：

```text
Host Byte Order
      ↓
Network Byte Order
```

当前只需记：

```text
设置端口时通常使用 htons()
```

反向转换：

```cpp
ntohs()
```

---

## Server 基本流程

```text
socket()
   ↓
bind()
   ↓
listen()
   ↓
accept()
```

### 1. `socket()`

```cpp
socket(AF_INET, SOCK_STREAM, 0);
```

创建一个：

```text
IPv4 + TCP Socket
```

成功后返回 Socket 对应的文件描述符。

### 2. `bind()`

```cpp
bind(sockfd, ...);
```

将 Socket 和本地的：

```text
IP + Port
```

绑定。

例如：

```text
sockfd
   ↓
127.0.0.1:8888
```

### 3. `listen()`

```cpp
listen(sockfd, SOMAXCONN);
```

让 Socket 开始监听客户端连接。

此时这个 Socket 称为：

```text
Listening Socket
监听 Socket
```

### 4. `accept()`

```cpp
int client_fd = accept(sockfd, ...);
```

等待并接受客户端连接。

没有客户端时：

```text
accept()
   ↓
阻塞等待
```

客户端连接成功后，`accept()` 会返回一个新的 fd：

```text
fd 3
│
└── Listening Socket
    继续监听新客户端

fd 4
│
└── Connected Socket
    和 Client A 通信
```

两种 fd 职责不同：

```text
监听 fd
→ 负责接收新连接

连接 fd
→ 负责和具体客户端通信
```

多个客户端时：

```text
                fd 4 ←→ Client A
               /
listen fd 3 ──+── fd 5 ←→ Client B
               \
                fd 6 ←→ Client C
```

---

## Client 基本流程

```text
socket()
   ↓
connect()
```

### 1. `connect()`

```cpp
connect(sockfd, ...);
```

客户端主动连接服务器指定的：

```text
IP + Port
```

例如：

```text
127.0.0.1:8888
```

---

## TCP通信

 ![img](data:image/svg+xml;charset=utf-8,%3Csvg%20font-family%3D%22-apple-system-body%2C%20ui-sans-serif%2C%20-apple-system%2C%20system-ui%2C%20Segoe%20UI%2C%20Helvetica%2C%20Apple%20Color%20Emoji%2C%20Arial%2C%20sans-serif%2C%20Segoe%20UI%20Emoji%2C%20Segoe%20UI%20Symbol%22%20font-weight%3D%22400%22%20data-d-component%3D%22svg%22%20fill%3D%22currentColor%22%20style%3D%22color%3Argb(237%2C%20237%2C%20237)%22%20viewBox%3D%220%200%20320%20112%22%20width%3D%22100%25%22%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%3E%3Ctext%20x%3D%22160%22%20y%3D%2213%22%20text-anchor%3D%22middle%22%20font-size%3D%2211%22%20fill%3D%22%2316a34a%22%3E%E5%AE%A2%E6%88%B7%E7%AB%AF%E5%8F%91%E9%80%81%E6%95%B0%E6%8D%AE%20%E2%86%92%20%E6%9C%8D%E5%8A%A1%E7%AB%AF%E6%8E%A5%E6%94%B6%E6%95%B0%E6%8D%AE%3C%2Ftext%3E%3Cpath%20d%3D%22M25%2034%20H294%22%20stroke%3D%22%2316a34a%22%20stroke-width%3D%222.5%22%20fill%3D%22none%22%2F%3E%3Cpath%20d%3D%22M282%2028%20L294%2034%20L282%2040%22%20stroke%3D%22%2316a34a%22%20stroke-width%3D%222.5%22%20fill%3D%22none%22%20stroke-linecap%3D%22round%22%20stroke-linejoin%3D%22round%22%2F%3E%3Ctext%20x%3D%2245%22%20y%3D%2228%22%20text-anchor%3D%22middle%22%20font-size%3D%2211%22%20fill%3D%22currentColor%22%3Ewrite()%3C%2Ftext%3E%3Ctext%20x%3D%22265%22%20y%3D%2228%22%20text-anchor%3D%22middle%22%20font-size%3D%2211%22%20fill%3D%22currentColor%22%3Eread()%3C%2Ftext%3E%3Cpath%20d%3D%22M294%2079%20H25%22%20stroke%3D%22%232563eb%22%20stroke-width%3D%222.5%22%20fill%3D%22none%22%2F%3E%3Cpath%20d%3D%22M37%2073%20L25%2079%20L37%2085%22%20stroke%3D%22%232563eb%22%20stroke-width%3D%222.5%22%20fill%3D%22none%22%20stroke-linecap%3D%22round%22%20stroke-linejoin%3D%22round%22%2F%3E%3Ctext%20x%3D%2245%22%20y%3D%2270%22%20text-anchor%3D%22middle%22%20font-size%3D%2211%22%20fill%3D%22currentColor%22%3Eread()%3C%2Ftext%3E%3Ctext%20x%3D%22265%22%20y%3D%2270%22%20text-anchor%3D%22middle%22%20font-size%3D%2211%22%20fill%3D%22currentColor%22%3Ewrite()%3C%2Ftext%3E%3Ctext%20x%3D%22160%22%20y%3D%22103%22%20text-anchor%3D%22middle%22%20font-size%3D%2211%22%20fill%3D%22%232563eb%22%3E%E5%AE%A2%E6%88%B7%E7%AB%AF%E6%8E%A5%E6%94%B6%E6%95%B0%E6%8D%AE%20%E2%86%90%20%E6%9C%8D%E5%8A%A1%E7%AB%AF%E5%8F%91%E9%80%81%E6%95%B0%E6%8D%AE%3C%2Ftext%3E%3C%2Fsvg%3E) 

## 错误处理

```cpp
/*
bool 传入判断条件，char* 传入报错信息
*/
void errif(bool condition, const char *errmsg){
    if(condition){
        perror(errmsg);
        exit(EXIT_FAILURE);
    }
}

errif(sockfd == -1, "socket create error");
```

## 多并发

### 1. 进程与线程

- 进程：运行中的程序，就被称为「进程」
- 线程：线程是进程当中的一条执行流程。

### 2. I/O 复用

所有的服务器都是高并发的，可以同时为成千上万个客户端提供服务（因为处理速度很快，拉开时间看就是高并发），这一技术又被称为 I/O 复用

实现方式：`epoll`，`poll`，`select`，[详细讲解](https://xiaolincoding.com/os/8_network_system/selete_poll_epoll.html#i-o-%E5%A4%9A%E8%B7%AF%E5%A4%8D%E7%94%A8)

### 3. I/O 多路复用三种方式

- `select`：把一组 fd 交给内核检查，返回后还要遍历所有 fd，找出哪些就绪；而且 fd 数量有限。
- `poll`：和 `select` 类似，也是把一组 fd 交给内核，再遍历找就绪 fd，但用数组保存 fd，没有固定的 1024 限制。
- `epoll`：先把 fd 注册到内核，之后只返回真正就绪的 fd，不需要每次把所有 fd 都传进去、再全部遍历。（红黑树+链表）

演进可以理解为：

```text
select
↓
poll：解决 fd 数量限制
↓
epoll：进一步避免每次全量传递和全量扫描
```

所以整体上是：

> `select/poll` 更像“每次都问一遍所有 fd”，`epoll` 更像“先登记，谁有事谁通知我”。

### 4. epoll 两种事件触发模式

**区分：**边缘触发（Edge Triggered，ET）和水平触发（Level Triggered，LT）

- 使用边缘触发模式时，当被监控的 Socket 描述符上有可读事件发生时，服务器端只会从 `epoll_wait` 中苏醒一次，即使进程没有调用 `read` 函数从内核读取数据，也依然只苏醒一次，因此我们程序要保证一次性将内核缓冲区的数据读取完；
- 使用水平触发模式时，当被监控的 Socket 上有可读事件发生时，服务器端不断地从 `epoll_wait` 中苏醒，直到内核缓冲区数据被 `read` 函数读完才结束，目的是告诉我们有数据需要读取；

**关于 ET 必须配合非阻塞 Socket：**

- 因为只通知一次，要尽可能读完，会用到循环，最后一次的时候 `read` 如果没有可读用阻塞会卡住

**epoll 默认 LT 触发，需要 ET 的话专门设置：**

```cpp
ev.data.fd = sockfd;
ev.events = EPOLLIN | EPOLLET;
```

### 5. “阻塞 Socket”和“非阻塞 Socket”

当你调用 `accept()`、`read()`、`recv()` 这类函数时，如果当前没东西可处理，程序到底是“等在那里”（阻塞），还是“立刻返回”（非阻塞）。

### 6. 基础代码

```cpp
// ET 模式通常配合非阻塞 fd 使用
void setnonblocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

// 1. 创建 epoll 实例
int epfd = epoll_create1(0);

// 用来描述“我要监听什么事件”
struct epoll_event ev;

// 用来接收 epoll_wait 返回的“已就绪事件”
struct epoll_event events[MAX_EVENTS];


// 2. 把服务器监听 socket 注册进 epoll
ev.data.fd = sockfd;              // 这个事件对应哪个 fd
ev.events = EPOLLIN | EPOLLET;    // 监听可读事件 + ET 边缘触发

setnonblocking(sockfd);

/*
EPOLL_CTL_ADD	添加 fd
EPOLL_CTL_MOD	修改 fd 的监听事件
EPOLL_CTL_DEL	删除 fd
*/
epoll_ctl(
    epfd,                 // 哪个 epoll
    EPOLL_CTL_ADD,        // 添加 fd
    sockfd,               // 要监听的 fd
    &ev                    // 监听什么事件
);

// 4. 进入事件循环
while (true) {

    // 等待事件发生
    int nfds = epoll_wait(
        epfd,
        events,           // 就绪事件会被写到这里
        MAX_EVENTS,
        -1                // -1：一直阻塞到有事件
    );

    // 5. 处理所有就绪 fd
    for (int i = 0; i < nfds; ++i) {

        int fd = events[i].data.fd;

        if (fd == sockfd) {
            // 有新客户端连接
            int client_fd = accept(...);

            // 把新连接也注册进 epoll
            ev.data.fd = client_fd;
            ev.events = EPOLLIN | EPOLLET;

            setnonblocking(client_fd);

            epoll_ctl(
                epfd,
                EPOLL_CTL_ADD,
                client_fd,
                &ev
            );
        }

        else if (events[i].events & EPOLLIN) {
            // 客户端 fd 可读
            // 循环 read，直到读完或遇到 EAGAIN
        }

        else {
            // 其他事件，后续课程继续处理
        }
    }
}
```

**对于epoll的理解**：

1. epoll用于关于server和client的基础操作，首先可以把它当作一个存储池。

   `比如ev会被反复利用，因为ev相当于临时变量，每次先临时存储需要的event，然后写入epoll中，epoll有了一份之后ev就没什么用了`

2. epoll 本身是持续 while(true) 运行直到有事情处理   `epoll_wait 一直阻塞到有事件`，否则就原地休眠

3. epoll_event 设置ET和LT 主要决定 在 epoll 等到了需要处理的事件之后的 读取方式，例如 ET 通知一次后，会尽量一次把数据读到 `EAGAIN` 

4. 返回值

```cpp
// 返回epoll实例的文件描述符
int epfd = epoll_create(1);

/*
0	操作成功
-1	操作失败，查看 errno
*/
int ret = epoll_ctl(epfd, op, fd, &ev);

/*
> 0	成功，返回本次获得的就绪事件数量
0	等待超时，没有获得就绪事件
-1	出错，具体原因查看 errno
*/
int nfds = epoll_wait(epfd, events, maxevents, timeout);
```

## C++ 指针创建对象

- C++ 中常见的内存区域有栈和堆。
  - 栈：通常存局部变量，函数结束后自动释放，空间相对较小。
  - 堆：通常通过 `new` 动态申请，需要程序员自己 `delete` 释放。
- Java 中 `Student student = new Student()` 一般对象放在堆，引用放在栈，如果对象没有被任何东西指向，有资格被回收

## `union` 类型

- 特点：多个不同类型的变量，共用同一块内存。
- 例如： epoll允许你选择一种方式来保存“用户数据”

```cpp
typedef union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;
```

## Channel 的设计

### 1. 联系

```cpp
// 之前
epoll_event ev;
ev.data.fd = 5;
ev.events = EPOLLIN;

epoll_ctl(..., &ev);

// 现在
epoll_event ev;
ev.data.ptr = channel;
ev.events = channel->getEvents();

epoll_ctl(..., &ev);
```

现在讲 `ev.data.fd = 5;` 变成 `ev.data.ptr = channel;` 存储一个 Channel 类的引用

>  `epoll_event` 仍然只是注册事件时使用的临时结构；`Channel` 则在应用层长期维护一个 fd 的事件状态和回调，并通过 `ev.data.ptr` 与 epoll 返回的事件关联起来。 

### 2. Channel 类

```cpp
class Channel{
private:
    Epoll *ep; // 指向分发到的 epoll
    int fd;  // 监听的 Socket
    uint32_t events;  // 希望监听这个文件描述符的哪些事件
    uint32_t revents;  // 返回该 Channel 时文件描述符正在发生的事件
    bool inEpoll;  // 表示当前 Channel 是否已经在 epoll 红黑树中
};
```

- `events` 和 `revents` 都用整数，用位 bit 代表 bool 事件是否发生

### 3. 回调函数

1. 概念：  把一个函数提前注册好，等某个事件发生时，再由其他代码调用它， 回调机制让 `Channel` 只需要知道如何调用函数，不需要知道这个函数究竟是干什么的。
2. 应用场景

```cpp
// server中的函数
    void handleReadEvent(int);
    void newConnection(Socket *serv_sock);

/* 
channel回调函数应用 
*/

// 最开始注册监听socket的时候，设置channel为 newConnection
    std::function<void()> cb = std::bind(&Server::newConnection, this, serv_sock);
    servChannel->setCallback(cb);

// 之后如果监听socket有可读信息，调用 newConnection 会创建通信socket，设置channel为 handleReadEvent
    std::function<void()> cb = std::bind(&Server::handleReadEvent, this, clnt_sock->getFd());
    clntChannel->setCallback(cb);
```

## EventLoop

### 1. 基本流程

1.  情况 A：新客户端连接服务器 

```
客户端调用 connect()
          ↓
内核完成 TCP 握手
          ↓
新连接进入服务端 accept 队列
          ↓
监听 Socket 变为可读
          ↓
epoll_wait() 返回对应事件
          ↓
Channel::handleEvent()
          ↓
Server::newConnection()
          ↓
accept()
          ↓
获得客户端 Socket
```

2.  情况 B：已经连接的客户端发送数据 

```
客户端 A 发送 hello
          ↓
数据进入服务端 Socket 接收缓冲区
          ↓
客户端 A 对应的 fd=5 变为可读
          ↓
epoll_wait() 返回对应事件
          ↓
Channel::handleEvent()
          ↓
Server::handleReadEvent()
          ↓
read()
          ↓
读取 hello
```

3. 基本流程

监听socket添加流程：

```
Server(loop)
  → 创建监听 Socket，bind()、listen()
  → 创建监听 Channel，绑定回调函数
  → Channel::enableReading()
  → EventLoop::updateChannel(this)
  → Epoll::updateChannel()
  → epoll_ctl(EPOLL_CTL_ADD)
```

![1791538185731](C:\Users\icovo\AppData\Roaming\Typora\typora-user-images\1791538185731.png)

## Reactor 和 Proactor

Reactor 是非阻塞同步网络模式，而 Proactor 是异步网络模式

- 同步网络模式

> Reactor 可以理解为「来了事件操作系统通知应用进程，让应用进程来处理」，无论是 ET 还是 LT。

![img](https://pic1.zhimg.com/80/v2-51e052e2beecef41da3aed3ebc2b80bd_720w.webp?source=2c26e567)

- 异步网络模式

> Proactor 可以理解为「来了事件操作系统来处理，处理完再通知应用进程」。

![img](https://picx.zhimg.com/80/v2-7f73fdcaca316aa0f12d77b6873785e5_720w.webp?source=2c26e567)

## Acceptor 解耦

### 1. 原因

Server类承担了最开始监听socket的一系列初始化等，现在解耦，新加入Acceptor类执行监听socket的一系列初始化

### 2. 调用顺序

```
Server(loop)
  → new Acceptor(loop)
      → 创建监听 Channel
      → Channel 绑定 Acceptor::acceptConnection
      → 注册到 epoll
  → Acceptor 绑定 Server::newConnection
  → Server 初始化完成

loop->loop()
  → Channel::handleEvent()
  → Acceptor::acceptConnection()
  → Server::newConnection()
  → Socket::accept()
```

### 3. 关于bind绑定回调函数

> `std::bind` 生成一个保存调用信息的对象，`std::function` 保存这个对象，真正执行时才调用目标函数 

## Connection

### 1. 原因

由监听socket监听到的socket原来由Server确定逻辑，现在分离出来，完成echo通信和初始化等等操作

### 2. 调用顺序

```
连接建立：
Acceptor::acceptConnection()
  → accept()
  → Server::newConnection()
  		→ new Connection(loop, sock)
  		→ conn->setDeleteConnectionCallback(cb); （绑定删除函数的对象，存储map）
  			→ 创建 Channel
  			→ channel 绑定 Connection::echo()
  			→ enableReading()
  
连接断开：
Connection::echo()
  → read() 返回 0
  → deleteConnectionCallback(sock)
  		→ Server::deleteConnection()
 	 		→ 从 map 移除
 	 		→ delete Connection
```

### 3. 分离原因

 为了降低类之间的耦合 ，设计思路： 职责分离 + 回调解耦 + 所有权管理 

## 内存管理

### 1. delete

```
   delete Socket
     → ~Socket()
     → close(fd) // 只关闭内核资源，不会删除 Socket、Channel、TcpConnection
     → 释放 Socket 对象内存
```

### 2. 智能指针

1. 思想： 用一个能够自动管理对象生命周期的 C++ 对象，代替需要手动 delete 的裸指针 
2.  unique_ptr

```cpp
#include <memory>

// 创建
auto sock = std::make_unique<Socket>();

// 使用
sock->setnonblocking();

/*
释放：离开作用域自动释放
unique_ptr 析构
       ↓
删除 Socket 对象  （删除自己）
       ↓
执行 Socket::~Socket()  （删除自身管理的对象）
       ↓
close(fd)
*/
Socket::~Socket() {
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}

// get函数：借用，指向同一个socket
Socket* q = p.get();

// move函数，转移所有权
auto sock = std::move(clientSocket);
```