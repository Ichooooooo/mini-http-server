#include "TcpConnection.h"
#include "Channel.h"
#include "Socket.h"

#include <cerrno>
#include <cstdio>
#include <unistd.h>
#include <sys/socket.h>
#include <utility>

TcpConnection::TcpConnection(EventLoop* loop, std::unique_ptr<Socket> socket)
    : sock(std::move(socket)),
      channel(std::make_unique<Channel>(loop, sock->getFd())), closed(false) {
    channel->setCallback([this] { handleReadEvent(); });
    channel->enableReading();
}

// 先销毁 Channel（注销事件），再由 Socket 析构关闭 fd；不在读回调中直接 close。
TcpConnection::~TcpConnection() = default;

void TcpConnection::handleReadEvent() {
    if (closed) {
        return;
    }

    const int sockfd = sock->getFd();
    char buf[1024];
    while (true) {
        const ssize_t bytesRead = read(sockfd, buf, sizeof(buf));
        if (bytesRead > 0) {
            printf("message from client fd %d: %.*s\n", sockfd,
                   static_cast<int>(bytesRead), buf);
            ssize_t bytesSent;
            do {
                // 单次调用抑制 SIGPIPE，仍通过返回值处理 EPIPE 等断连错误。
                bytesSent = send(sockfd, buf, bytesRead, MSG_NOSIGNAL);
            } while (bytesSent == -1 && errno == EINTR);
            if (bytesSent == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("send");
                handleClose();
                return;
            }
            // 短写 / EAGAIN 的剩余字节尚未保存，下一步用 Buffer 和 EPOLLOUT 补齐。
        } else if (bytesRead == -1 && errno == EINTR) {
            continue; // 系统调用被信号打断，重试读取。
        } else if (bytesRead == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break; // 当前没有更多可读数据，返回 EventLoop。
        } else if (bytesRead == 0) {
            printf("EOF, client fd %d disconnected\n", sockfd);
            handleClose();
            return;
        } else {
            perror("read");
            handleClose();
            return;
        }
    }
}

void TcpConnection::handleClose() {
    if (closed) {
        return;
    }
    closed = true;
    channel->remove();
    if (deleteConnectionCallback) {
        deleteConnectionCallback(sock->getFd());
    }
}

void TcpConnection::setDeleteConnectionCallback(std::function<void(int)> callback) {
    deleteConnectionCallback = std::move(callback);
}
