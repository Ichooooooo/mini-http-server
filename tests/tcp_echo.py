"""Day 8 TCP Echo 回归；运行：python3 tests/tcp_echo.py <server 可执行文件>。"""

import argparse
import contextlib
import pathlib
import socket
import subprocess
import tempfile
import time


ADDRESS = ("127.0.0.1", 8888)


def connect():
    return socket.create_connection(ADDRESS, timeout=3)


def receive_exact(client, size):
    result = bytearray()
    while len(result) < size:
        part = client.recv(size - len(result))
        assert part, "服务器提前断开"
        result.extend(part)
    return bytes(result)


def echo(client, payload):
    client.sendall(payload)
    assert receive_exact(client, len(payload)) == payload, "Echo 内容不一致"


def fd_count(pid):
    return len(list(pathlib.Path(f"/proc/{pid}/fd").iterdir()))


def wait_for_fd_count(process, expected):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        assert process.poll() is None, "服务器异常退出"
        if fd_count(process.pid) == expected:
            return
        time.sleep(0.01)
    raise AssertionError(f"fd 未回收：期望 {expected}，实际 {fd_count(process.pid)}")


def run(binary):
    # 先检查固定端口，避免把已有服务器误当成本次测试进程。
    with socket.socket() as probe:
        probe.bind(ADDRESS)

    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([str(binary)], stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 3
            while True:
                assert process.poll() is None, "服务器启动失败"
                try:
                    with connect() as client:
                        echo(client, b"ready")
                    break
                except ConnectionRefusedError:
                    assert time.monotonic() < deadline, "服务器启动超时"
                    time.sleep(0.02)

            # 当前程序固定持有 stdin/stdout/stderr、epoll、监听 Socket。
            wait_for_fd_count(process, 5)
            baseline = 5
            with connect() as client:
                echo(client, b"hello")
                echo(client, b"x" * 1024)  # 填满读缓冲，不带字符串结束符。
                echo(client, bytes(range(256)) * 4)  # 包含零字节。
            print("PASS: 文本、1024 字节满缓冲、二进制 Echo")

            with contextlib.ExitStack() as stack:
                clients = [stack.enter_context(connect()) for _ in range(8)]
                for index, client in enumerate(clients):
                    echo(client, f"client-{index}".encode())
                for index, client in enumerate(clients):
                    echo(client, f"second-{index}".encode())
            wait_for_fd_count(process, baseline)
            print("PASS: 8 个同时保持连接的客户端，重复收发并断开")

            for _ in range(100):
                with connect() as client:
                    echo(client, b"reconnect")
            wait_for_fd_count(process, baseline)
            print("PASS: 100 次连接/断开，fd 数回到基线")

            with connect() as client:
                payload = b"echo before EOF"
                client.sendall(payload)
                client.shutdown(socket.SHUT_WR)
                assert receive_exact(client, len(payload)) == payload
                assert client.recv(1) == b"", "EOF 后未关闭连接"
            wait_for_fd_count(process, baseline)
            print("PASS: 小消息后半关闭，收到 Echo 和 EOF")

            with connect() as client:
                echo(client, b"still alive")
            wait_for_fd_count(process, baseline)
            print("PASS: 断开后新连接仍能正常收发")
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            log.seek(0)
            output = log.read().decode(errors="replace")
            if process.returncode != -15 or "Sanitizer" in output or "runtime error:" in output:
                raise AssertionError(f"服务器退出异常或 Sanitizer 报错：\n{output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    args = parser.parse_args()
    run(args.binary.resolve())
