#include "EventLoop.h"
#include "Epoll.h"
#include "Channel.h"
#include <vector>
#include <utility>

EventLoop::EventLoop() : ep(nullptr), quit(false){
    ep = new Epoll();
}

EventLoop::~EventLoop()
{
    delete ep;
}


void EventLoop::loop(){
    while(!quit){
        std::vector<Channel*> chs;
        chs = ep->poll();
        for(auto it = chs.begin(); it != chs.end(); ++it){
            (*it)->handleEvent();
        }
        // 所有就绪 Channel 的回调返回后，才能释放本批次中的连接。
        auto callbacks = std::move(pendingFunctors);
        pendingFunctors.clear();
        for (auto& callback : callbacks) {
            callback();
        }
    }
}

void EventLoop::updateChannel(Channel *ch){
    ep->updateChannel(ch);
}

void EventLoop::removeChannel(Channel *ch){
    ep->removeChannel(ch);
}

void EventLoop::queueInLoop(std::function<void()> callback){
    pendingFunctors.push_back(std::move(callback));
}
