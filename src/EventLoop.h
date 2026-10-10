#pragma once
#include <functional>
#include <vector>
class Epoll;
class Channel;
class EventLoop
{
private:
    Epoll *ep;
    bool quit;
    std::vector<std::function<void()>> pendingFunctors;
public:
    EventLoop();
    ~EventLoop();

    void loop();
    void updateChannel(Channel*);
    void removeChannel(Channel*);
    // 只用于本 EventLoop 线程：当前批次事件结束后执行，不提供跨线程唤醒。
    void queueInLoop(std::function<void()> callback);
};

