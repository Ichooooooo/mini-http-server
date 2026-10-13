#include "TcpConnection.h"
#include "Channel.h"
#include "Socket.h"

#include <cerrno>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

TcpConnection::TcpConnection(EventLoop* loop, std::unique_ptr<Socket> socket)
    : sock(std::move(socket)),
      channel(std::make_unique<Channel>(loop, sock->getFd())),
      closed(false), closing(false) {
    channel->setCallback([this] { handleEvent(); });
    channel->enableReading();
}

// 先销毁 Channel（注销事件），再由 Socket 析构关闭 fd；不在读回调中直接 close。
TcpConnection::~TcpConnection() = default;

void TcpConnection::handleEvent() {
    if (closed) {
        return;
    }
    const uint32_t revents = channel->getRevents();
    if (revents & EPOLLERR) {
        int socketError = 0;
        socklen_t length = sizeof(socketError);
        if (getsockopt(sock->getFd(), SOL_SOCKET, SO_ERROR, &socketError, &length) == -1) {
            perror("socket error query");
            handleClose();
            return;
        }
        if (socketError != 0) {
            errno = socketError;
            perror("socket");
            handleClose();
            return;
        }
    }
    // HUP 可能与未读取的数据同时出现，不能直接跳过读处理。
    if (revents & (EPOLLIN | EPOLLHUP)) {
        handleReadEvent();
    }
    if (closed) {
        return; // 读处理可能已关闭连接，不能继续执行写处理。
    }
    if (revents & (EPOLLOUT | EPOLLHUP)) {
        handleWriteEvent();
    }
}

void TcpConnection::send(const char* data, std::size_t length) {
    if (closed || closing || length == 0) {
        return;
    }

    std::size_t sent = 0;
    if (outputBuffer.readableBytes() == 0) {
        ssize_t bytesSent;

        do {
            bytesSent = ::send(sock->getFd(), data, length, MSG_NOSIGNAL);
        } while (bytesSent == -1 && errno == EINTR);

        if (bytesSent > 0) {
            sent = static_cast<std::size_t>(bytesSent);
        } else if (bytesSent == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("send");
                handleClose();
                return;
            }
        } else {
            std::fputs("send made no progress\n", stderr);
            handleClose();
            return;
        }
    }

    // 有积压时不直接发送新数据，保证新数据排在旧数据之后。
    if (sent < length) {
        outputBuffer.append(data + sent, length - sent);
        if (!(channel->getEvents() & EPOLLOUT)) {
            channel->enableWriting();
        }
    }
}

void TcpConnection::handleReadEvent() {
    if (closed || closing) {
        return;
    }

    const int sockfd = sock->getFd();
    char buf[1024];
    while (true) {
        const ssize_t bytesRead = read(sockfd, buf, sizeof(buf));
        if (bytesRead > 0) {
            printf("message from client fd %d: %.*s\n", sockfd,
                   static_cast<int>(bytesRead), buf);
            send(buf, static_cast<std::size_t>(bytesRead));
            if (closed) {
                return;
            }
        } else if (bytesRead == -1 && errno == EINTR) {
            continue; // 系统调用被信号打断，重试读取。
        } else if (bytesRead == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break; // 当前没有更多可读数据，返回 EventLoop。
        } else if (bytesRead == 0) {
            printf("EOF, client fd %d disconnected\n", sockfd);
            closeAfterFlush();
            return;
        } else {
            perror("read");
            handleClose();
            return;
        }
    }
}

void TcpConnection::handleWriteEvent() {
    if (closed) {
        return;
    }
    while (outputBuffer.readableBytes() != 0) {
        const ssize_t bytesSent = ::send(sock->getFd(), outputBuffer.peek(),
                                         outputBuffer.readableBytes(), MSG_NOSIGNAL);
        if (bytesSent > 0) {
            outputBuffer.retrieve(static_cast<std::size_t>(bytesSent));
        } else if (bytesSent == -1 && errno == EINTR) {
            continue;
        } else if (bytesSent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return; // 保留未发送的数据和 EPOLLOUT，等待下一次可写事件。
        } else {
            if (bytesSent == -1) {
                perror("send");
            } else {
                std::fputs("send made no progress\n", stderr);
            }
            handleClose();
            return;
        }
    }

    channel->disableWriting();
    if (closing) {
        handleClose();
    }
}

void TcpConnection::closeAfterFlush() {
    if (closed || closing) {
        return;
    }
    closing = true;
    channel->disableReading();
    if (outputBuffer.readableBytes() == 0) {
        handleClose();
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
