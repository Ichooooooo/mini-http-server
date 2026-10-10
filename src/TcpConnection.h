#pragma once

#include <functional>
#include <memory>

class EventLoop;
class Socket;
class Channel;

// 对应教程 Day 8 的 Connection：管理一个客户端的资源与 Echo 读回调。
class TcpConnection {
private:
    std::unique_ptr<Socket> sock;
    std::unique_ptr<Channel> channel;
    bool closed;
    std::function<void(int)> deleteConnectionCallback;

    void handleClose();

public:
    TcpConnection(EventLoop* loop, std::unique_ptr<Socket> socket);
    ~TcpConnection();

    void handleReadEvent();
    void setDeleteConnectionCallback(std::function<void(int)> callback);
};
