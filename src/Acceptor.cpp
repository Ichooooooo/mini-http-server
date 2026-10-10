#include "Acceptor.h"
#include "Channel.h"
#include "InetAddress.h"
#include "Socket.h"
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
    // 实际 accept() 和客户端管理仍由 Server 完成。
    if (newConnectionCallback) {
        newConnectionCallback(sock.get());
    }
}

void Acceptor::setNewConnectionCallback(std::function<void(Socket*)> callback) {
    newConnectionCallback = std::move(callback);
}
