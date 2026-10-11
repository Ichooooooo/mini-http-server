#include "Buffer.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void expectContent(const Buffer& buffer, const std::string& expected) {
    assert(buffer.readableBytes() == expected.size());
    assert(std::string(buffer.peek(), buffer.readableBytes()) == expected);
}

void testEmptyAndZeroLength() {
    Buffer buffer;
    expectContent(buffer, "");
    buffer.append(nullptr, 0);
    buffer.retrieve(0);
    buffer.retrieveAll();
    expectContent(buffer, "");

    buffer.append("hello", 5);
    buffer.retrieve(2);
    buffer.append(nullptr, 0);
    buffer.retrieve(0);
    expectContent(buffer, "llo");
}

void testAppendAndConsume() {
    Buffer buffer;
    buffer.append("hello", 5);
    buffer.append("world", 5);
    expectContent(buffer, "helloworld");
    buffer.retrieve(2);
    expectContent(buffer, "lloworld");
    buffer.retrieve(3);
    expectContent(buffer, "world");
    buffer.retrieve(buffer.readableBytes());
    expectContent(buffer, "");

    buffer.append("again", 5);
    expectContent(buffer, "again");
    buffer.retrieve(1);
    buffer.retrieveAll();
    expectContent(buffer, "");
    buffer.append("new", 3);
    expectContent(buffer, "new");
}

void testBinaryAndSourceLifetime() {
    Buffer buffer;
    const std::string expected("a\0b\0c", 5);
    {
        std::string source = expected;
        buffer.append(source.data(), source.size());
        source.assign(source.size(), 'x');
    }
    expectContent(buffer, expected);
    buffer.retrieve(2);
    expectContent(buffer, expected.substr(2));
}

void testCompactionAndGrowth() {
    Buffer buffer;
    buffer.append("abcdef", 6);
    buffer.retrieve(4);
    buffer.append("gh", 2);
    expectContent(buffer, "efgh");

    const std::string large(128 * 1024, 'x');
    buffer.append(large.data(), large.size());
    expectContent(buffer, std::string("efgh") + large);
    buffer.retrieve(4 + large.size() / 2);
    buffer.append("tail", 4);
    expectContent(buffer, large.substr(large.size() / 2) + "tail");
}

void testInvalidLengths() {
    Buffer buffer;
    buffer.append("hello", 5);
    buffer.retrieve(2);
    for (const std::size_t length : {std::size_t(4),
                                     std::numeric_limits<std::size_t>::max()}) {
        bool rejected = false;
        try {
            buffer.retrieve(length);
        } catch (const std::out_of_range&) {
            rejected = true;
        }
        assert(rejected);
        expectContent(buffer, "llo");
    }

    bool rejected = false;
    try {
        buffer.append(nullptr, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    expectContent(buffer, "llo");

    buffer.retrieveAll();
    rejected = false;
    try {
        buffer.retrieve(1);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    assert(rejected);
    expectContent(buffer, "");
}

void testRepeatedAppendAndConsume() {
    Buffer buffer;
    const std::string chunk("x\0yz", 4);
    buffer.append(chunk.data(), chunk.size());
    for (int i = 0; i < 1000; ++i) {
        buffer.retrieve(2);
        buffer.append(chunk.data(), 2);
        expectContent(buffer, std::string("yzx\0", 4));
        buffer.retrieve(2);
        buffer.append(chunk.data() + 2, 2);
        expectContent(buffer, chunk);
    }
}
} // namespace

int main() {
    testEmptyAndZeroLength();
    testAppendAndConsume();
    testBinaryAndSourceLifetime();
    testCompactionAndGrowth();
    testInvalidLengths();
    testRepeatedAppendAndConsume();
    puts("PASS: Buffer 追加、部分/全部消费、二进制、压缩扩容、错误边界和重复使用");
}
