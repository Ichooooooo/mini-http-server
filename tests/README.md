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

`peek()` 返回借用指针，必须配合 `readableBytes()` 使用；修改或销毁 Buffer 后需要重新获取。`append()` 的来源不能指向同一个 Buffer 的内部存储。TcpConnection 已将 Buffer 用作 Output Buffer，但独立 Buffer 测试本身不验证网络发送，需同时运行下面的发送回归。

## 非阻塞发送与排空关闭

```sh
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Wpedantic -g -Isrc tests/connection_write.cpp src/*.cpp -Wl,--wrap=send,--wrap=epoll_ctl,--wrap=epoll_wait -o build/connection-write
timeout 15s ./build/connection-write

g++ -std=c++17 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc tests/connection_write.cpp src/*.cpp -Wl,--wrap=send,--wrap=epoll_ctl,--wrap=epoll_wait -o build/connection-write-asan
ASAN_OPTIONS=detect_leaks=1 timeout 15s ./build/connection-write-asan
```

`connection_write.cpp` 使用非阻塞 socketpair，不占用 TCP 端口。send 的 `--wrap` 只在测试中限制真实发送长度或注入错误，epoll_ctl/epoll_wait 仍调用真实系统调用，并记录事件注册与就绪通知。覆盖：

- 小消息与二进制直接发送，零长度发送不调用系统调用。
- 首次发送和写回调的 EINTR 重试、短写、EAGAIN；剩余数据不丢失、不重复。
- 有积压时后续数据排在尾部；来源修改后仍可正确发送；部分消费后追加保持顺序。
- 有积压才开启 EPOLLOUT，发送完成后取消写事件且保留读事件；用另一个 Channel 唤醒一批事件，确认已清空的连接没有无效可写通知。
- 主动 `closeAfterFlush()` 和 EOF：停止读取，积压发送完成后通知删除，批次结束后真正销毁；重复关闭或关闭后发送不重复处理。
- 真实背压：SO_SNDBUF 设置为 4096，暂不读取 512 KiB 二进制消息，观察真实短写/EAGAIN，再逐批读取并核对完整内容；同一 EventLoop 中另一个连接在慢连接未读取时仍能 Echo。
- 积压期间对端断开；send 零进展时安全收尾而不是循环空转。测试使用默认 SIGPIPE 行为，依赖 MSG_NOSIGNAL 存活。

故障用例中的 `Broken pipe` 和 `send made no progress` 是预期错误日志，不代表测试失败。socketpair 的背压证据与真实 TCP 回归互补，不将普通大消息 TCP 测试当作进入 EAGAIN 路径的证明。

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

`tcp_echo.py` 检查文本、满缓冲与二进制 Echo、256 KiB 大消息及后续同连接收发、多客户端重复收发、100 次断连后的 fd 回收、小消息及 256 KiB 消息半关闭、20 次发送后 RST 和后续新连接。脚本最后用 SIGTERM 结束服务器，只检查运行中的 ASan/UBSan 报错，不作为该进程的泄漏检查。正常退出的 C++ 测试能够运行 LeakSanitizer。

`syscall_errors.cpp` 的 `--wrap` 仅作用于测试可执行文件：在指定位置注入 accept/fcntl/epoll_wait/send 错误，其他调用仍使用真实系统调用。send 测试先注入 EINTR，再真实发送；随后让对端先发数据再关闭，验证真实 EPIPE。测试将 SIGPIPE 设置为默认行为，保证存活依赖发送端的 MSG_NOSIGNAL。可用 `fcntl`、`accept`、`epoll`、`send` 或 `pipe` 参数单独运行；`pipe` 只检查真实断连路径。

`connection_lifecycle.cpp` 检查延迟销毁、关闭后重复读回调、EOF 前的小消息 Echo，以及 Server 析构释放尚未处理 EOF 的连接。测试用异常退出无限事件循环，让局部对象正常析构；这是测试退出方式。

## 既有系统调用修复证据

以 `38b26b5` 的源码构建相同故障测试，已复现：fcntl 被注入 EINTR 后非阻塞标志未设置；accept 的 EAGAIN 经 errif 退出；epoll_wait 的 EINTR 经 errif 退出；`pipe` 用例被 SIGPIPE 结束（信号 13）。修改后的错误测试、真实 TCP 回归和生命周期测试均通过，ASan/UBSan 与两个正常退出测试的 LeakSanitizer 未报告错误。故障注入验证指定错误路径，不代表已覆盖全部系统或网络错误。

参考 [Wlgls Day 8](https://github.com/Wlgls/30daysCppWebServer/tree/main/code/day08) 保留组件分工，本批是错误处理修复。accept 改为由 Acceptor 判断 errno 并接收到 EAGAIN；fcntl 分别检查 GETFL/SETFL；epoll_wait 的 EINTR 返回空批次，由 EventLoop 继续等待；发送改为 send(MSG_NOSIGNAL)，EINTR 重试，断连错误进入现有延迟关闭流程。错误语义参考 Linux 手册：[accept](https://man7.org/linux/man-pages/man2/accept.2.html)、[epoll_wait](https://man7.org/linux/man-pages/man2/epoll_wait.2.html)、[send](https://man7.org/linux/man-pages/man2/sendto.2.html)。

## 本批输出缓冲验证与限制

TcpConnection 已接入 Output Buffer：无积压时先尝试发送，短写/EAGAIN 的剩余部分复制到缓冲；有积压时新数据追加到尾部。统一事件入口分发读写事件，EPOLLERR 查询 SO_ERROR，不可恢复错误进入关闭流程；EOF 或主动 closeAfterFlush 停止读取，待输出清空后保留原有批次结束后的延迟销毁机制。

本批普通构建和 ASan/UBSan 下的 Buffer、发送、系统调用错误、生命周期及真实 TCP 回归均通过；正常退出的 C++ 测试的 LeakSanitizer 未报告错误。发送测试实际观察到真实短写/EAGAIN，并验证排空、顺序和事件关闭。检查结果仅适用于这些场景，不表示已覆盖全部网络错误或证明没有缺陷。

当前仍未实现 Input Buffer、HTTP 或输入/输出积压上限；慢客户端长期不读取可能持续占用内存。accept 的其他错误记录后返回，持续 EMFILE/ENFILE 等资源耗尽仍需限流或退避处理；启动失败及 epoll 的非 EINTR 错误仍沿用 errif 终止。上述测试不包含这些资源耗尽场景。
