#include "src/EventLoop.h"
#include "src/Server.h"

int main() {
    // 构造时先创建 EventLoop，退出作用域时先销毁 Server。
    EventLoop loop;
    Server server(&loop);
    loop.loop();
    return 0;
}
