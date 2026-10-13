#pragma once

#include "Buffer.h"
#include <functional>
#include <memory>

class EventLoop;
class Socket;
class Channel;

// 管理一个客户端的资源、Echo 读写和发送完成后的关闭。
class TcpConnection {
private:
    std::unique_ptr<Socket> sock;
    std::unique_ptr<Channel> channel;
    Buffer outputBuffer;
    bool closed; // 已进入最终关闭流程，不再处理 I/O。
    bool closing; // 正在排空输出，不再读取或接受新输出。
    std::function<void(int)> deleteConnectionCallback;

    void handleEvent();
    void handleWriteEvent();
    void handleClose();

public:
    TcpConnection(EventLoop* loop, std::unique_ptr<Socket> socket);
    ~TcpConnection();

    void handleReadEvent();
    // data 必须指向 length 个有效字节；未发送部分会复制到 outputBuffer。
    // 关闭流程开始后不再接受新数据，length 为 0 时直接返回。
    void send(const char* data, std::size_t length);
    // 停止读取，已有输出发送完成后进入原有的延迟销毁流程。
    void closeAfterFlush();
    void setDeleteConnectionCallback(std::function<void(int)> callback);
};
