#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity);

    // 写入数据，返回实际写入字节数
    size_t Write(const uint8_t* data, size_t len);

    // 读取数据但不弹出，返回实际读取字节数
    size_t Peek(uint8_t* out, size_t len) const;

    // 弹出数据
    void Pop(size_t len);

    // 当前可用数据量
    size_t Size() const;

    // 剩余空间
    size_t Free() const;

    // 总容量
    size_t Capacity() const;

private:
    std::vector<uint8_t> buffer_;
    size_t head_, tail_, size_;
};