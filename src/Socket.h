#pragma once

class InetAddress;
class Socket
{
private:
    int fd;
public:
    Socket();
    Socket(int);
    ~Socket();

    void bind(InetAddress*);
    void listen();
    // 失败时返回 false 并保留 errno，由调用者决定关闭连接还是终止启动。
    bool setnonblocking();

    // 原样返回 accept 的结果；失败时由 Acceptor 判断 errno。
    int accept(InetAddress*);

    int getFd();
};

