#pragma once

#include <cstddef>
#include <string>

// 保存未消费的字节，不负责 Socket 读写或协议解析。
class Buffer {
private:
    std::string storage;
    std::size_t readIndex = 0;

public:
    Buffer();
    ~Buffer();

    // 复制 length 个字节（包括 '\0'）；length 为 0 时不做操作。
    // length 非零时，data 必须有效且不能指向当前 Buffer 内部的存储；
    // data 为 nullptr 时抛出 std::invalid_argument。
    void append(const char* data, std::size_t length);
    std::size_t readableBytes() const;

    // 借用未消费数据；修改或销毁 Buffer 后应重新获取，配合 readableBytes 使用。
    const char* peek() const;

    // 超过可读长度时抛出 std::out_of_range，保持原有数据不变。
    void retrieve(std::size_t length);
    void retrieveAll();
};
