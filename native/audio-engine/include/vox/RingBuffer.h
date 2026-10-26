#pragma once
// Lock-free single-producer / single-consumer ring buffer of floats. The capacity is fixed at
// construction (control thread); push() (audio thread) and pop() (writer thread) never allocate
// or block. push() writes as much as fits and reports the rest as dropped, so an overflow is
// detected and reported instead of silently corrupting a recording.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace vox
{
class SpscRingBuffer
{
public:
    explicit SpscRingBuffer (size_t capacity = 0) { reset (capacity); }

    /** Not thread safe: call only while neither side is active. */
    void reset (size_t capacity)
    {
        buf.assign (capacity + 1, 0.0f);
        head.store (0);
        tail.store (0);
    }

    size_t capacity() const noexcept { return buf.empty() ? 0 : buf.size() - 1; }

    size_t availableToRead() const noexcept
    {
        const size_t h = head.load (std::memory_order_acquire), t = tail.load (std::memory_order_acquire);
        return h >= t ? h - t : h + buf.size() - t;
    }
    size_t availableToWrite() const noexcept { return capacity() - availableToRead(); }

    /** Producer side. Returns the number of samples written (< n means overflow). */
    size_t push (const float* data, size_t n) noexcept
    {
        if (buf.empty()) return 0;
        const size_t h = head.load (std::memory_order_relaxed);
        const size_t t = tail.load (std::memory_order_acquire);
        const size_t size = buf.size();
        const size_t free = (t + size - h - 1) % size;
        n = std::min (n, free);
        const size_t first = std::min (n, size - h);
        std::copy (data, data + first, buf.begin() + (std::ptrdiff_t) h);
        std::copy (data + first, data + n, buf.begin());
        head.store ((h + n) % size, std::memory_order_release);
        return n;
    }

    /** Consumer side. Returns the number of samples read. */
    size_t pop (float* out, size_t n) noexcept
    {
        if (buf.empty()) return 0;
        const size_t t = tail.load (std::memory_order_relaxed);
        const size_t h = head.load (std::memory_order_acquire);
        const size_t size = buf.size();
        const size_t avail = (h + size - t) % size;
        n = std::min (n, avail);
        const size_t first = std::min (n, size - t);
        std::copy (buf.begin() + (std::ptrdiff_t) t, buf.begin() + (std::ptrdiff_t) (t + first), out);
        std::copy (buf.begin(), buf.begin() + (std::ptrdiff_t) (n - first), out + first);
        tail.store ((t + n) % size, std::memory_order_release);
        return n;
    }

private:
    std::vector<float> buf;
    std::atomic<size_t> head { 0 }, tail { 0 };
};

} // namespace vox
