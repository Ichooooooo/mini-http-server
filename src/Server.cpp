#include "Server.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "Socket.h"
#include "TcpConnection.h"

#include <utility>

Server::Server(EventLoop* _loop)
    : loop(_loop), acceptor(std::make_unique<Acceptor>(_loop)) {
    acceptor->setNewConnectionCallback(
        [this](std::unique_ptr<Socket> sock) { newConnection(std::move(sock)); });
}

// 必须在 loop 停止后销毁 Server，并且 EventLoop 要比 Server 活得更久。
Server::~Server() = default;

void Server::newConnection(std::unique_ptr<Socket> sock) {
    const int sockfd = sock->getFd();
    auto connection = std::make_unique<TcpConnection>(loop, std::move(sock));
    connection->setDeleteConnectionCallback(
        [this](int fd) { deleteConnection(fd); });
    connections.emplace(sockfd, std::move(connection));
}

void Server::deleteConnection(int sockfd) {
    // 不能在 TcpConnection 自己的读回调中立即 erase，当前批次仍可能持有 Channel*。
    loop->queueInLoop([this, sockfd] { connections.erase(sockfd); });
}
