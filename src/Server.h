#pragma once

#include <memory>

class EventLoop;
class Socket;
class Acceptor;
class Server
{
private:
    EventLoop *loop;
    std::unique_ptr<Acceptor> acceptor;
public:
    Server(EventLoop*);
    ~Server();

    void handleReadEvent(int);
    void newConnection(Socket *serv_sock);
};

