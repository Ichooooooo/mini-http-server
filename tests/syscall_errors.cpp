#include "Acceptor.h"
#include "Channel.h"
#include "Epoll.h"
#include "EventLoop.h"
#include "Socket.h"
#include "TcpConnection.h"

#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

// 仅测试可执行文件通过 --wrap 注入故障，生产代码仍调用真实系统调用。
namespace {
std::deque<int> getFlagErrors;
std::deque<int> setFlagErrors;
std::deque<int> acceptResults;
bool mockAccept = false;
bool interruptPoll = false;
bool interruptSend = false;
int sendCalls = 0;

void expectClosed(int fd) {
    errno = 0;
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
}

struct Pair {
    std::unique_ptr<Socket> local;
    std::unique_ptr<Socket> peer;
    Pair() {
        int fds[2];
        const int result = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        assert(result == 0);
        local = std::make_unique<Socket>(fds[0]);
        peer = std::make_unique<Socket>(fds[1]);
    }
};
} // namespace

extern "C" int __real_fcntl(int, int, ...);
extern "C" int __real_accept(int, sockaddr*, socklen_t*);
extern "C" int __real_epoll_wait(int, epoll_event*, int, int);
extern "C" ssize_t __real_send(int, const void*, size_t, int);

extern "C" int __wrap_fcntl(int fd, int command, ...) {
    auto& errors = command == F_GETFL ? getFlagErrors : setFlagErrors;
    if ((command == F_GETFL || command == F_SETFL) && !errors.empty()) {
        errno = errors.front();
        errors.pop_front();
        return -1;
    }
    if (command == F_SETFL) {
        va_list args;
        va_start(args, command);
        const int flags = va_arg(args, int);
        va_end(args);
        return __real_fcntl(fd, command, flags);
    }
    return __real_fcntl(fd, command);
}

extern "C" int __wrap_accept(int fd, sockaddr* addr, socklen_t* length) {
    if (!mockAccept) {
        return __real_accept(fd, addr, length);
    }
    if (acceptResults.empty()) {
        errno = EAGAIN;
        return -1;
    }
    const int result = acceptResults.front();
    acceptResults.pop_front();
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

extern "C" int __wrap_epoll_wait(int fd, epoll_event* events, int count, int timeout) {
    if (interruptPoll) {
        interruptPoll = false;
        errno = EINTR;
        return -1;
    }
    return __real_epoll_wait(fd, events, count, timeout);
}

extern "C" ssize_t __wrap_send(int fd, const void* bytes, size_t size, int flags) {
    ++sendCalls;
    assert(flags & MSG_NOSIGNAL);
    if (interruptSend) {
        interruptSend = false;
        errno = EINTR;
        return -1;
    }
    return __real_send(fd, bytes, size, flags);
}

namespace {
void testFcntl() {
    Pair pair;
    getFlagErrors = {EINTR};
    setFlagErrors = {EINTR};
    pair.local->setnonblocking();
    assert(getFlagErrors.empty() && setFlagErrors.empty());
    assert(fcntl(pair.local->getFd(), F_GETFL) & O_NONBLOCK);

    Pair failed;
    getFlagErrors = {EIO};
    failed.local->setnonblocking();
    assert(errno == EIO);
    assert(!(fcntl(failed.local->getFd(), F_GETFL) & O_NONBLOCK));
    setFlagErrors = {EIO};
    failed.local->setnonblocking();
    assert(errno == EIO);
    assert(!(fcntl(failed.local->getFd(), F_GETFL) & O_NONBLOCK));
    puts("PASS: fcntl EINTR 重试，GETFL/SETFL 失败保留错误且不误设非阻塞");
}

void testAccept() {
    EventLoop loop;
    Acceptor acceptor(&loop);
    std::vector<std::unique_ptr<Socket>> clients;
    acceptor.setNewConnectionCallback([&](std::unique_ptr<Socket> socket) {
        assert(fcntl(socket->getFd(), F_GETFL) & O_NONBLOCK);
        clients.push_back(std::move(socket));
    });
    mockAccept = true;
    acceptor.acceptConnection(); // 就绪后队列可能已空，不能退出进程。
    assert(clients.empty());

    std::vector<std::unique_ptr<Socket>> peers;
    auto pendingClient = [&] {
        int fds[2];
        const int result = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        assert(result == 0);
        peers.push_back(std::make_unique<Socket>(fds[1]));
        return fds[0]; // 这个原始 fd 随后由 Acceptor 包装并接管。
    };
    const int rejectedFd = pendingClient();
    const int acceptedFd = pendingClient();
    acceptResults = {-EINTR, -ECONNABORTED, -EPROTO, rejectedFd, acceptedFd, -EAGAIN};
    setFlagErrors = {EIO};
    acceptor.acceptConnection();
    expectClosed(rejectedFd);
    assert(clients.size() == 1 && acceptResults.empty());

    acceptResults = {-EACCES};
    acceptor.acceptConnection();
    acceptResults = {pendingClient(), -EAGAIN};
    acceptor.acceptConnection();
    assert(clients.size() == 2);
    mockAccept = false;
    puts("PASS: accept EAGAIN/临时错误，非阻塞设置失败回收 fd，其他错误后可继续接收");
}

void testEpoll() {
    struct Ready {};
    EventLoop loop;
    Pair pair;
    Channel channel(&loop, pair.local->getFd());
    channel.setCallback([] { throw Ready{}; });
    channel.enableReading();
    interruptPoll = true;
    const char byte = 'x';
    const ssize_t sent = write(pair.peer->getFd(), &byte, 1);
    assert(sent == 1);
    try {
        loop.loop();
        assert(false);
    } catch (const Ready&) {
    }
    assert(!interruptPoll);
    puts("PASS: epoll_wait EINTR 返回空批次，下一次 poll 正常取得事件");
}

void testSend(bool testRetry = true) {
    // 明确采用默认 SIGPIPE 处理；测试必须靠 MSG_NOSIGNAL 存活。
    assert(signal(SIGPIPE, SIG_DFL) != SIG_ERR);
    EventLoop loop;
    Pair pair;
    pair.local->setnonblocking();
    auto connection = std::make_unique<TcpConnection>(&loop, std::move(pair.local));
    int closeCalls = 0;
    connection->setDeleteConnectionCallback([&](int) { ++closeCalls; });
    const char payload[] = "echo";
    const ssize_t sent = write(pair.peer->getFd(), payload, sizeof(payload));
    assert(sent == static_cast<ssize_t>(sizeof(payload)));
    interruptSend = testRetry;
    sendCalls = 0;
    connection->handleReadEvent();
    assert(closeCalls == 0);
    if (testRetry) {
        assert(sendCalls == 2);
    }
    char echoed[sizeof(payload)];
    const ssize_t received = recv(pair.peer->getFd(), echoed, sizeof(echoed), MSG_WAITALL);
    assert(received == static_cast<ssize_t>(sizeof(payload)));
    for (size_t i = 0; i < sizeof(payload); ++i) {
        assert(echoed[i] == payload[i]);
    }

    // 数据先入接收队列，然后关闭对端；read 成功，但 send 确定会遇到断连。
    const ssize_t lastSent = write(pair.peer->getFd(), payload, sizeof(payload));
    assert(lastSent == static_cast<ssize_t>(sizeof(payload)));
    pair.peer.reset();
    const int before = sendCalls;
    connection->handleReadEvent();
    assert(sendCalls == before + 1 && closeCalls == 1);
    connection->handleReadEvent();
    assert(closeCalls == 1);
    puts("PASS: send EINTR 重试、真实 EPIPE 不触发 SIGPIPE，仅关闭一次连接");
}
} // namespace

int main(int argc, char** argv) {
    const std::string test = argc == 2 ? argv[1] : "all";
    assert(test == "all" || test == "fcntl" || test == "accept" ||
           test == "epoll" || test == "send" || test == "pipe");
    if (test == "all" || test == "fcntl") testFcntl();
    if (test == "all" || test == "accept") testAccept();
    if (test == "all" || test == "epoll") testEpoll();
    if (test == "all" || test == "send") testSend();
    if (test == "pipe") testSend(false);
}
