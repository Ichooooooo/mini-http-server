#include "Channel.h"
#include "EventLoop.h"

Channel::Channel(EventLoop *_loop, int _fd) : loop(_loop), fd(_fd), events(0), revents(0), inEpoll(false){

}

Channel::~Channel()
{
    remove();
}

void Channel::handleEvent(){
    callback();
}

void Channel::enableReading(){
    events |= EPOLLIN;
    loop->updateChannel(this);
}

void Channel::disableReading(){
    if (!(events & EPOLLIN)) {
        return;
    }
    events &= ~EPOLLIN;
    loop->updateChannel(this);
}

void Channel::enableWriting(){
    events |= EPOLLOUT;
    loop->updateChannel(this);
}

void Channel::disableWriting(){
    if (!(events & EPOLLOUT)) {
        return;
    }
    events &= ~EPOLLOUT;
    loop->updateChannel(this);
}

void Channel::remove(){
    if (inEpoll) {
        loop->removeChannel(this);
    }
    events = 0;
    revents = 0;
}

int Channel::getFd(){
    return fd;
}

uint32_t Channel::getEvents(){
    return events;
}
uint32_t Channel::getRevents(){
    return revents;
}

bool Channel::getInEpoll(){
    return inEpoll;
}

void Channel::setInEpoll(bool registered){
    inEpoll = registered;
}

// void Channel::setEvents(uint32_t _ev){
//     events = _ev;
// }

void Channel::setRevents(uint32_t _ev){
    revents = _ev;
}

void Channel::setCallback(std::function<void()> _cb){
    callback = _cb;
}
