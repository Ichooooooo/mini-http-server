#pragma once

#include <memory>
#include <map>

class EventLoop;
class Socket;
class Acceptor;
class TcpConnection;
class Server
{
private:
    EventLoop *loop;
    std::unique_ptr<Acceptor> acceptor;
    std::map<int, std::unique_ptr<TcpConnection>> connections;
public:
    Server(EventLoop*);
    ~Server();

    void newConnection(std::unique_ptr<Socket> sock);
    void deleteConnection(int sockfd);
};

