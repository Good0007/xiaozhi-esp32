#include "raing_buffer.h"
#include <algorithm>

RingBuffer::RingBuffer(size_t capacity)
    : buffer_(capacity), head_(0), tail_(0), size_(0) {}

size_t RingBuffer::Write(const uint8_t* data, size_t len) {
    size_t written = 0;
    while (written < len && size_ < buffer_.size()) {
        buffer_[tail_] = data[written++];
        tail_ = (tail_ + 1) % buffer_.size();
        ++size_;
    }
    return written;
}

size_t RingBuffer::Peek(uint8_t* out, size_t len) const {
    size_t read = 0, idx = head_;
    while (read < len && read < size_) {
        out[read++] = buffer_[idx];
        idx = (idx + 1) % buffer_.size();
    }
    return read;
}

void RingBuffer::Pop(size_t len) {
    size_t pop_len = std::min(len, size_);
    head_ = (head_ + pop_len) % buffer_.size();
    size_ -= pop_len;
}

size_t RingBuffer::Size() const {
    return size_;
}

size_t RingBuffer::Free() const {
    return buffer_.size() - size_;
}

size_t RingBuffer::Capacity() const {
    return buffer_.size();
}