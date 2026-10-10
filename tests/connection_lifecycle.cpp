#include "EventLoop.h"
#include "Server.h"
#include "Socket.h"
#include "TcpConnection.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
// 测试专用：从延迟任务退出无限事件循环，使局部对象正常析构并运行 LeakSanitizer。
struct BatchFinished {};

void expectClosed(int fd) {
    errno = 0;
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
}

void testDeferredClose() {
    EventLoop loop;
    int fds[2];
    const int result = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(result == 0);
    Socket peer(fds[1]);
    auto socket = std::make_unique<Socket>(fds[0]);
    socket->setnonblocking();
    auto connection = std::make_unique<TcpConnection>(&loop, std::move(socket));
    int closeCalls = 0;

    connection->setDeleteConnectionCallback([&](int fd) {
        ++closeCalls;
        assert(fd == fds[0]);
        assert(fcntl(fd, F_GETFD) != -1); // 回调期间 Socket 仍活着。
        connection->handleReadEvent(); // 已关闭的连接再次进入读回调应立即返回。
        assert(closeCalls == 1);
        loop.queueInLoop([&] {
            connection.reset();
            expectClosed(fds[0]);
            throw BatchFinished{};
        });
        assert(connection); // queueInLoop 尚未执行，不能在自己的回调里销毁。
    });

    const char payload[] = {'h', '\0', 'i'};
    const ssize_t sent = write(peer.getFd(), payload, sizeof(payload));
    assert(sent == static_cast<ssize_t>(sizeof(payload)));
    const int shutdownResult = shutdown(peer.getFd(), SHUT_WR);
    assert(shutdownResult == 0);
    try {
        loop.loop();
        assert(false);
    } catch (const BatchFinished&) {
    }
    assert(!connection && closeCalls == 1);

    char echoed[sizeof(payload)];
    const ssize_t received = recv(peer.getFd(), echoed, sizeof(echoed), MSG_WAITALL);
    assert(received == static_cast<ssize_t>(sizeof(payload)));
    for (size_t i = 0; i < sizeof(payload); ++i) {
        assert(echoed[i] == payload[i]);
    }
    char byte;
    const ssize_t eof = read(peer.getFd(), &byte, 1);
    assert(eof == 0);
}

void testServerDestruction() {
    EventLoop loop;
    std::vector<int> clientFds;
    {
        // loop 未运行，Server 析构也必须释放连接表里尚未处理 EOF 的对象。
        Server server(&loop);
        for (int i = 0; i < 32; ++i) {
            int fds[2];
            const int result = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
            assert(result == 0);
            Socket peer(fds[1]);
            auto socket = std::make_unique<Socket>(fds[0]);
            socket->setnonblocking();
            clientFds.push_back(fds[0]);
            server.newConnection(std::move(socket));
        }
    }
    for (int fd : clientFds) {
        expectClosed(fd);
    }
}
} // namespace

int main() {
    testDeferredClose();
    testServerDestruction();
    puts("PASS: 延迟销毁、重复读回调、EOF Echo、Server 析构释放 32 个连接");
}
