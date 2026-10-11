#include "Buffer.h"

#include <stdexcept>

Buffer::Buffer() = default;
Buffer::~Buffer() = default;

void Buffer::append(const char* data, std::size_t length) {
    if (length == 0) {
        return;
    }
    if (data == nullptr) {
        throw std::invalid_argument("Buffer append requires non-null data");
    }

    // 部分消费只移动索引；再次追加时移除已消费的前缀。
    if (readIndex != 0) {
        storage.erase(0, readIndex);
        readIndex = 0;
    }
    storage.append(data, length);
}

std::size_t Buffer::readableBytes() const {
    return storage.size() - readIndex;
}

const char* Buffer::peek() const {
    return storage.data() + readIndex;
}

void Buffer::retrieve(std::size_t length) {
    const std::size_t available = readableBytes();
    if (length > available) {
        throw std::out_of_range("Buffer retrieve exceeds readable bytes");
    }
    if (length == available) {
        retrieveAll();
    } else {
        readIndex += length;
    }
}

void Buffer::retrieveAll() {
    storage.clear();
    readIndex = 0;
}
