#pragma once

#include <functional>
#include <memory>

class EventLoop;
class Socket;
class Channel;

class Acceptor {
private:
    std::unique_ptr<Socket> sock;
    std::unique_ptr<Channel> acceptChannel;
    std::function<void(Socket*)> newConnectionCallback;

public:
    explicit Acceptor(EventLoop* loop);
    ~Acceptor();

    void acceptConnection();
    void setNewConnectionCallback(std::function<void(Socket*)> callback);
};
