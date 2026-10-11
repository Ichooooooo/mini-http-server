#include "Acceptor.h"
#include "Channel.h"
#include "InetAddress.h"
#include "Socket.h"
#include "util.h"
#include <cerrno>
#include <cstdio>
#include <utility>

Acceptor::Acceptor(EventLoop* loop)
    : sock(std::make_unique<Socket>()),
      acceptChannel(std::make_unique<Channel>(loop, sock->getFd())) {
    InetAddress addr("127.0.0.1", 8888);
    sock->bind(&addr);
    sock->listen();
    errif(!sock->setnonblocking(), "listening socket nonblocking error");

    acceptChannel->setCallback([this] { acceptConnection(); });
    acceptChannel->enableReading();
}

// Server 和 Acceptor 在事件循环停止后销毁。
// 按成员声明的逆序先销毁 Channel，再由 Socket 析构关闭监听 fd。
Acceptor::~Acceptor() = default;

void Acceptor::acceptConnection() {
    while (true) {
        InetAddress addr;
        const int fd = sock->accept(&addr);
        if (fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return; // 接收队列暂时为空，回到 EventLoop。
            }
            // Linux 会把部分连接级网络错误直接从 accept 返回，继续接收下一条。
            if (errno == EINTR || errno == ECONNABORTED || errno == ENETDOWN ||
                errno == EPROTO || errno == ENOPROTOOPT || errno == EHOSTDOWN ||
                errno == ENONET || errno == EHOSTUNREACH || errno == EOPNOTSUPP ||
                errno == ENETUNREACH) {
                continue;
            }
            perror("socket accept error");
            return;
        }

        auto clientSocket = std::make_unique<Socket>(fd);
        if (!clientSocket->setnonblocking()) {
            perror("client socket nonblocking error");
            continue; // unique_ptr 自动关闭这个未交付的客户端 fd。
        }
        printf("new client fd %d! IP: %s Port: %d\n", fd,
               inet_ntoa(addr.addr.sin_addr), ntohs(addr.addr.sin_port));
        if (newConnectionCallback) {
            newConnectionCallback(std::move(clientSocket));
        }
    }
}

void Acceptor::setNewConnectionCallback(
    std::function<void(std::unique_ptr<Socket>)> callback) {
    newConnectionCallback = std::move(callback);
}
