# Buffer 与当前 TCP Echo 回归

在仓库根目录顺序执行。TCP 测试和包含 Acceptor/Server 的 C++ 测试需要 127.0.0.1:8888 空闲；独立 Buffer 测试不需要网络端口。

## 独立 Buffer 测试

```sh
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Wpedantic -g -Isrc tests/buffer.cpp src/Buffer.cpp -o build/buffer
timeout 10s ./build/buffer

g++ -std=c++17 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tests/buffer.cpp src/Buffer.cpp -o build/buffer-asan
ASAN_OPTIONS=detect_leaks=1 timeout 10s ./build/buffer-asan
```

`buffer.cpp` 检查初始为空、零长度操作、顺序追加、部分/全部消费、清空后复用、包含零字节的数据、追加内容不依赖来源生命周期、压缩与扩容、超量消费和非零长度空指针报错，以及 1000 轮部分消费后追加。超量消费抛出 `std::out_of_range`，非零长度空指针追加抛出 `std::invalid_argument`，这些错误不改变可读内容。

`peek()` 返回借用指针，必须配合 `readableBytes()` 使用；修改或销毁 Buffer 后需要重新获取。`append()` 的来源不能指向同一个 Buffer 的内部存储。本批 Buffer 是独立组件，尚未接入 TcpConnection，不代表短写、发送 EAGAIN 或 EPOLLOUT 已实现。

## 普通构建

```sh
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Wpedantic -g server.cpp src/*.cpp -o build/server
python3 tests/tcp_echo.py build/server

g++ -std=c++17 -Wall -Wextra -Wpedantic -g -Isrc tests/syscall_errors.cpp src/*.cpp -Wl,--wrap=accept,--wrap=fcntl,--wrap=epoll_wait,--wrap=send -o build/syscall-errors
timeout 10s ./build/syscall-errors

g++ -std=c++17 -Wall -Wextra -Wpedantic -g -Isrc tests/connection_lifecycle.cpp src/*.cpp -o build/lifecycle
timeout 10s ./build/lifecycle
```

## ASan / UBSan / LeakSanitizer

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer server.cpp src/*.cpp -o build/server-asan
python3 tests/tcp_echo.py build/server-asan

g++ -std=c++17 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tests/syscall_errors.cpp src/*.cpp -Wl,--wrap=accept,--wrap=fcntl,--wrap=epoll_wait,--wrap=send -o build/errors-asan
ASAN_OPTIONS=detect_leaks=1 timeout 10s ./build/errors-asan

g++ -std=c++17 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tests/connection_lifecycle.cpp src/*.cpp -o build/lifecycle-asan
ASAN_OPTIONS=detect_leaks=1 timeout 10s ./build/lifecycle-asan
```

`tcp_echo.py` 检查文本、满缓冲与二进制 Echo、多客户端重复收发、100 次断连后的 fd 回收、小消息半关闭、20 次发送后 RST 和后续新连接。脚本最后用 SIGTERM 结束服务器，只检查运行中的 ASan/UBSan 报错，不作为该进程的泄漏检查。两个 C++ 测试正常退出，能够运行 LeakSanitizer。

`syscall_errors.cpp` 的 `--wrap` 仅作用于测试可执行文件：在指定位置注入 accept/fcntl/epoll_wait/send 错误，其他调用仍使用真实系统调用。send 测试先注入 EINTR，再真实发送；随后让对端先发数据再关闭，验证真实 EPIPE。测试将 SIGPIPE 设置为默认行为，保证存活依赖发送端的 MSG_NOSIGNAL。可用 `fcntl`、`accept`、`epoll`、`send` 或 `pipe` 参数单独运行；`pipe` 只检查真实断连路径。

`connection_lifecycle.cpp` 检查延迟销毁、关闭后重复读回调、EOF 前的小消息 Echo，以及 Server 析构释放尚未处理 EOF 的连接。测试用异常退出无限事件循环，让局部对象正常析构；这是测试退出方式。

## 本批修复证据

以 `38b26b5` 的源码构建相同故障测试，已复现：fcntl 被注入 EINTR 后非阻塞标志未设置；accept 的 EAGAIN 经 errif 退出；epoll_wait 的 EINTR 经 errif 退出；`pipe` 用例被 SIGPIPE 结束（信号 13）。修改后的错误测试、真实 TCP 回归和生命周期测试均通过，ASan/UBSan 与两个正常退出测试的 LeakSanitizer 未报告错误。故障注入验证指定错误路径，不代表已覆盖全部系统或网络错误。

参考 [Wlgls Day 8](https://github.com/Wlgls/30daysCppWebServer/tree/main/code/day08) 保留组件分工，本批是错误处理修复。accept 改为由 Acceptor 判断 errno 并接收到 EAGAIN；fcntl 分别检查 GETFL/SETFL；epoll_wait 的 EINTR 返回空批次，由 EventLoop 继续等待；发送改为 send(MSG_NOSIGNAL)，EINTR 重试，断连错误进入现有延迟关闭流程。错误语义参考 Linux 手册：[accept](https://man7.org/linux/man-pages/man2/accept.2.html)、[epoll_wait](https://man7.org/linux/man-pages/man2/epoll_wait.2.html)、[send](https://man7.org/linux/man-pages/man2/sendto.2.html)。

当前仍未实现输出缓冲和 EPOLLOUT：短写或发送 EAGAIN 时剩余数据可能丢失，慢客户端与大响应尚未通过验收。accept 的其他错误记录后返回；持续 EMFILE/ENFILE 等资源耗尽仍需后续限流或退避处理。启动失败及 epoll 的非 EINTR 错误仍沿用 errif 终止。上述测试不包含这些资源耗尽场景，也不代表 HTTP 已实现。
