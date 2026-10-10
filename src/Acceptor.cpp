#include "Acceptor.h"
#include "Channel.h"
#include "InetAddress.h"
#include "Socket.h"
#include <cstdio>
#include <utility>

Acceptor::Acceptor(EventLoop* loop)
    : sock(std::make_unique<Socket>()),
      acceptChannel(std::make_unique<Channel>(loop, sock->getFd())) {
    InetAddress addr("127.0.0.1", 8888);
    sock->bind(&addr);
    sock->listen();
    sock->setnonblocking();

    acceptChannel->setCallback([this] { acceptConnection(); });
    acceptChannel->enableReading();
}

// Server 和 Acceptor 在事件循环停止后销毁。
// 按成员声明的逆序先销毁 Channel，再由 Socket 析构关闭监听 fd。
Acceptor::~Acceptor() = default;

void Acceptor::acceptConnection() {
    InetAddress addr;
    auto clientSocket = std::make_unique<Socket>(sock->accept(&addr));
    printf("new client fd %d! IP: %s Port: %d\n", clientSocket->getFd(),
           inet_ntoa(addr.addr.sin_addr), ntohs(addr.addr.sin_port));
    clientSocket->setnonblocking();

    // 与 Day 8 一样，这里交付的是客户端 Socket；move 明确转移所有权。
    if (newConnectionCallback) {
        newConnectionCallback(std::move(clientSocket));
    }
}

void Acceptor::setNewConnectionCallback(
    std::function<void(std::unique_ptr<Socket>)> callback) {
    newConnectionCallback = std::move(callback);
}
