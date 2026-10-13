#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"
#include "TcpConnection.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

// --wrap 只影响本测试：正数限制真实发送长度，负数注入 errno。
namespace {
int watchedFd = -1;
uint32_t watchedEvents = 0;
bool watchedRegistered = false;
int sendCalls = 0;
int shortWrites = 0;
int wouldBlock = 0;
int writableNotifications = 0;
std::deque<int> sendResults;
std::vector<std::size_t> requestedSizes;

struct BatchFinished {};

struct Pair {
    std::unique_ptr<Socket> local;
    std::unique_ptr<Socket> peer;
    Pair() {
        int fds[2];
        const int result = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        assert(result == 0);
        local = std::make_unique<Socket>(fds[0]);
        peer = std::make_unique<Socket>(fds[1]);
        const bool localNonblocking = local->setnonblocking();
        const bool peerNonblocking = peer->setnonblocking();
        assert(localNonblocking && peerNonblocking);
    }
};

void watch(int fd) {
    watchedFd = fd;
    watchedEvents = 0;
    watchedRegistered = false;
    sendCalls = shortWrites = wouldBlock = writableNotifications = 0;
    sendResults.clear();
    requestedSizes.clear();
}

std::string receiveAvailable(int fd) {
    std::string received;
    char buf[4096];
    while (true) {
        const ssize_t count = recv(fd, buf, sizeof(buf), 0);
        if (count > 0) {
            received.append(buf, static_cast<std::size_t>(count));
        } else if (count == -1 && errno == EINTR) {
            continue;
        } else {
            assert(count == 0 || (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)));
            return received;
        }
    }
}

// 通过真实事件进入写回调，批次结束后退出，使局部对象能够正常析构。
void runBatch(EventLoop& loop) {
    loop.queueInLoop([] { throw BatchFinished{}; });
    try {
        loop.loop();
        assert(false);
    } catch (const BatchFinished&) {
    }
}

void expectNoWritableNotification(EventLoop& loop) {
    Pair wake;
    Channel channel(&loop, wake.local->getFd());
    channel.setCallback([] {});
    channel.enableReading();
    const char byte = 'x';
    const ssize_t sent = write(wake.peer->getFd(), &byte, 1);
    assert(sent == 1);
    const int before = writableNotifications;
    runBatch(loop);
    assert(writableNotifications == before);
}

void expectClosed(int fd) {
    errno = 0;
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
}
} // namespace

extern "C" ssize_t __real_send(int, const void*, size_t, int);
extern "C" int __real_epoll_ctl(int, int, int, epoll_event*);
extern "C" int __real_epoll_wait(int, epoll_event*, int, int);

extern "C" ssize_t __wrap_send(int fd, const void* data, size_t size, int flags) {
    assert(flags & MSG_NOSIGNAL);
    if (fd != watchedFd) {
        return __real_send(fd, data, size, flags);
    }
    ++sendCalls;
    requestedSizes.push_back(size);
    ssize_t result;
    if (!sendResults.empty()) {
        const int action = sendResults.front();
        sendResults.pop_front();
        if (action < 0) {
            errno = -action;
            result = -1;
        } else if (action == 0) {
            result = 0;
        } else {
            result = __real_send(fd, data, std::min(size, static_cast<size_t>(action)), flags);
        }
    } else {
        result = __real_send(fd, data, size, flags);
    }
    if (result > 0 && static_cast<size_t>(result) < size) {
        ++shortWrites;
    } else if (result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        ++wouldBlock;
    }
    return result;
}

extern "C" int __wrap_epoll_ctl(int epfd, int operation, int fd, epoll_event* event) {
    const int result = __real_epoll_ctl(epfd, operation, fd, event);
    if (result == 0 && fd == watchedFd) {
        watchedRegistered = operation != EPOLL_CTL_DEL;
        watchedEvents = watchedRegistered ? event->events : 0;
    }
    return result;
}

extern "C" int __wrap_epoll_wait(int epfd, epoll_event* events, int count, int timeout) {
    const int result = __real_epoll_wait(epfd, events, count, timeout);
    for (int i = 0; i < result; ++i) {
        auto* channel = static_cast<Channel*>(events[i].data.ptr);
        if (channel->getFd() == watchedFd && (events[i].events & EPOLLOUT)) {
            ++writableNotifications;
        }
    }
    return result;
}

namespace {
void testDirectSend() {
    EventLoop loop;
    Pair pair;
    watch(pair.local->getFd());
    TcpConnection connection(&loop, std::move(pair.local));
    connection.send(nullptr, 0);
    assert(sendCalls == 0);
    const std::string payload("a\0b\0c", 5);
    connection.send(payload.data(), payload.size());
    assert(sendCalls == 1 && watchedEvents == EPOLLIN);
    assert(receiveAvailable(pair.peer->getFd()) == payload);
    expectNoWritableNotification(loop);
    connection.closeAfterFlush();
    connection.closeAfterFlush();
    assert(!watchedRegistered);
}

void testShortWriteAndOrdering() {
    EventLoop loop;
    Pair pair;
    watch(pair.local->getFd());
    TcpConnection connection(&loop, std::move(pair.local));
    std::string source = "abcdef";
    sendResults = {-EINTR, 2};
    connection.send(source.data(), source.size());
    source.assign(source.size(), 'x'); // 积压必须是独立副本，不能依赖来源。
    assert(sendCalls == 2 && sendResults.empty());
    assert(requestedSizes[0] == 6 && requestedSizes[1] == 6);
    assert(watchedEvents == (EPOLLIN | EPOLLOUT));
    assert(receiveAvailable(pair.peer->getFd()) == "ab");
    connection.send("XY", 2);
    assert(sendCalls == 2); // 有积压时，新数据不能绕过旧数据直接发送。

    sendResults = {-EINTR, 1, -EAGAIN};
    runBatch(loop);
    assert(sendResults.empty());
    assert(requestedSizes[2] == 6 && requestedSizes[3] == 6 && requestedSizes[4] == 5);
    assert(receiveAvailable(pair.peer->getFd()) == "c");
    assert(watchedEvents == (EPOLLIN | EPOLLOUT));
    connection.send("Z", 1); // 部分消费后追加，触发 Buffer 前缀压缩。
    runBatch(loop);
    assert(receiveAvailable(pair.peer->getFd()) == "defXYZ");
    assert(watchedEvents == EPOLLIN);
    expectNoWritableNotification(loop);
}

void testInitialWouldBlock() {
    EventLoop loop;
    Pair pair;
    watch(pair.local->getFd());
    TcpConnection connection(&loop, std::move(pair.local));
    sendResults = {-EAGAIN};
    connection.send("first", 5);
    connection.send("second", 6);
    assert(sendCalls == 1 && watchedEvents == (EPOLLIN | EPOLLOUT));
    assert(receiveAvailable(pair.peer->getFd()).empty());
    runBatch(loop);
    assert(receiveAvailable(pair.peer->getFd()) == "firstsecond");
    assert(watchedEvents == EPOLLIN);
}

void testCloseAfterFlush(bool eof) {
    EventLoop loop;
    Pair pair;
    const int fd = pair.local->getFd();
    watch(fd);
    auto connection = std::make_unique<TcpConnection>(&loop, std::move(pair.local));
    int closeCalls = 0;
    connection->setDeleteConnectionCallback([&](int closedFd) {
        assert(closedFd == fd && fcntl(fd, F_GETFD) != -1);
        ++closeCalls;
        connection->handleReadEvent();
        connection->closeAfterFlush();
        assert(closeCalls == 1);
        loop.queueInLoop([&] {
            connection.reset();
            expectClosed(fd);
            throw BatchFinished{};
        });
        assert(connection); // 不能在自己的写回调中立即销毁。
    });
    const std::string payload("reply\0after", 11);
    sendResults = {-EAGAIN};
    if (eof) {
        const ssize_t sent = write(pair.peer->getFd(), payload.data(), payload.size());
        assert(sent == static_cast<ssize_t>(payload.size()));
        const int result = shutdown(pair.peer->getFd(), SHUT_WR);
        assert(result == 0);
        connection->handleReadEvent();
    } else {
        connection->send(payload.data(), payload.size());
        connection->closeAfterFlush();
    }
    assert(closeCalls == 0 && watchedEvents == EPOLLOUT);
    const int before = sendCalls;
    connection->send("ignored", 7);
    connection->handleReadEvent();
    connection->closeAfterFlush();
    assert(sendCalls == before && closeCalls == 0);
    try {
        loop.loop();
        assert(false);
    } catch (const BatchFinished&) {
    }
    assert(closeCalls == 1 && !watchedRegistered && !connection);
    expectClosed(fd);
    assert(receiveAvailable(pair.peer->getFd()) == payload);
    char byte;
    const ssize_t count = recv(pair.peer->getFd(), &byte, 1, 0);
    assert(count == 0);
}

void testRealBackpressure() {
    EventLoop loop;
    Pair pair;
    const int fd = pair.local->getFd();
    const int sendBufferSize = 4096;
    const int result = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBufferSize,
                                 sizeof(sendBufferSize));
    assert(result == 0);
    watch(fd);
    auto connection = std::make_unique<TcpConnection>(&loop, std::move(pair.local));
    int closeCalls = 0;
    connection->setDeleteConnectionCallback([&](int) { ++closeCalls; });
    std::string payload(512 * 1024, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(i % 256);
    }
    connection->send(payload.data(), payload.size());
    assert(shortWrites > 0 && watchedEvents == (EPOLLIN | EPOLLOUT));

    // 慢连接暂不读取；另一个连接仍应在同一个 EventLoop 中完成 Echo。
    Pair healthy;
    TcpConnection healthyConnection(&loop, std::move(healthy.local));
    const ssize_t sent = write(healthy.peer->getFd(), "healthy", 7);
    assert(sent == 7);
    runBatch(loop);
    assert(receiveAvailable(healthy.peer->getFd()) == "healthy");
    assert(closeCalls == 0);

    connection->closeAfterFlush();
    std::string received = receiveAvailable(pair.peer->getFd());
    int batches = 0;
    while (watchedEvents & EPOLLOUT) {
        ++batches;
        assert(batches < 1024);
        runBatch(loop);
        received += receiveAvailable(pair.peer->getFd());
    }
    assert(received == payload && closeCalls == 1 && wouldBlock > 0);
    assert(!watchedRegistered);
    connection.reset();
    expectClosed(fd);
}

void testDisconnectedPeer() {
    EventLoop loop;
    Pair pair;
    watch(pair.local->getFd());
    TcpConnection connection(&loop, std::move(pair.local));
    int closeCalls = 0;
    connection.setDeleteConnectionCallback([&](int) { ++closeCalls; });
    sendResults = {-EAGAIN};
    connection.send("pending", 7);
    pair.peer.reset();
    runBatch(loop);
    assert(closeCalls == 1 && !watchedRegistered);
    const int before = sendCalls;
    connection.send("ignored", 7);
    connection.handleReadEvent();
    connection.closeAfterFlush();
    assert(sendCalls == before && closeCalls == 1);
}

void testZeroProgress() {
    EventLoop loop;
    Pair pair;
    watch(pair.local->getFd());
    TcpConnection connection(&loop, std::move(pair.local));
    int closeCalls = 0;
    connection.setDeleteConnectionCallback([&](int) { ++closeCalls; });
    sendResults = {-EAGAIN};
    connection.send("pending", 7);
    sendResults = {0};
    runBatch(loop);
    assert(closeCalls == 1 && !watchedRegistered && sendResults.empty());
}
} // namespace

int main() {
    const auto previous = std::signal(SIGPIPE, SIG_DFL);
    assert(previous != SIG_ERR);
    testDirectSend();
    testShortWriteAndOrdering();
    testInitialWouldBlock();
    testCloseAfterFlush(false);
    testCloseAfterFlush(true);
    testRealBackpressure();
    testDisconnectedPeer();
    testZeroProgress();
    puts("PASS: 完整发送、短写/EINTR/EAGAIN、顺序、写事件开关、排空关闭、真实背压和断连");
}
