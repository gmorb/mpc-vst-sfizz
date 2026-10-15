// SPDX-License-Identifier: BSD-2-Clause

// Copyright (c) 2019-2020, Paul Ferrand, Andrea Zanellato
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once
#include "Config.h"
#include "utility/LeakDetector.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <algorithm>
#include <utility>
#include <atomic>
#if defined(__linux__)
#include <sys/mman.h>
#endif

#ifdef DEBUG
#include <iostream>
#endif
namespace sfz
{

/**
 * @brief      A buffer counting class that tries to track the memory usage.
 */
class BufferCounter
{
public:
    /**
     * @brief      Return the buffer counter object.
     */
    static BufferCounter& counter() noexcept
    {
        static BufferCounter counter;
        return counter;
    }

protected:
    BufferCounter() noexcept = default;
    ~BufferCounter() noexcept
    {
#ifndef NDEBUG
        std::cout << "Remaining buffers on destruction: " << numBuffers << '\n';
        std::cout << "Total size: " << bytes << '\n';
#endif
    }

public:
    template <class I>
    void newBuffer(I size) noexcept
    {
        ++numBuffers;
        bytes.fetch_add(static_cast<size_t>(size));
    }

    template <class I>
    void bufferResized(I oldSize, I newSize) noexcept
    {
        bytes.fetch_add(static_cast<size_t>(newSize));
        bytes.fetch_sub(static_cast<size_t>(oldSize));
    }

    template <class I>
    void bufferDeleted(I size) noexcept
    {
        --numBuffers;
        bytes.fetch_sub(static_cast<size_t>(size));
    }

    size_t getNumBuffers() const noexcept { return numBuffers; }
    size_t getTotalBytes() const noexcept { return bytes; }
private:
    std::atomic<size_t> numBuffers { 0 };
    std::atomic<size_t> bytes { 0 };
};



/**
 * @brief      A heap buffer structure that tries to align its beginning and
 *             adds a small offset at the end for alignment too.
 *
 *             Apparently on Linux this effort is mostly useless, and in the end
 *             most of the SIMD operations are coded with alignment checks and
 *             sentinels so this class could probably be much simpler. It does
 *             however wrap realloc which in some cases should be a bit more
 *             efficient than allocating a whole new block.
 *
 * @tparam     Type       The buffer type
 * @tparam     Alignment  the required alignment in bytes (defaults to
 *                        config::defaultAlignment)
 */
template <class Type, unsigned int Alignment = config::defaultAlignment>
class Buffer {
public:
    using value_type = typename std::remove_cv<Type>::type;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using size_type = size_t;

    /**
     * @brief Construct a new Buffer object that is empty
     *
     */
    Buffer() noexcept
    {
    }

    /**
     * @brief Construct a new Buffer object with size
     *
     * @param size
     */
    explicit Buffer(size_t size)
    {
        resize(size);
    }

    /**
     * @brief Resizes the buffer. Given that std::realloc may return either the same pointer
     * or a new one, you need to account for both cases in the code if you are using deep pointers.
     *
     * @param newSize the new buffer size in bytes
     * @return true if allocation succeeded
     * @return false otherwise
     */
    bool resize(size_t newSize, std::nothrow_t) noexcept
    {
        if (newSize == 0) {
            clear();
            return true;
        }

        Type* oldData = paddedData.get();
        Type* oldNormalData = normalData;
        std::size_t oldSize = alignedSize;
        const std::size_t oldMapped = paddedData.get_deleter().mapped;

        std::size_t tempSize = newSize + 2 * AlignmentMask; // To ensure that we have leeway at the beginning and at the end
        std::size_t newMapped = 0;
        Type* newData = reinterpret_cast<Type*>(rawAllocate(tempSize * sizeof(value_type), newMapped));
        if (newData == nullptr) {
            return false;
        }

        if (largerSize > 0)
            counter().bufferResized(largerSize * sizeof(value_type), tempSize * sizeof(value_type));
        else
            counter().newBuffer(tempSize * sizeof(value_type));

        largerSize = tempSize;
        alignedSize = newSize;
        paddedData.release(); // realloc has invalidated the old pointer
        paddedData.reset(static_cast<pointer>(newData));
        paddedData.get_deleter().mapped = newMapped;
        normalData = static_cast<pointer>(align(Alignment, alignedSize, newData, tempSize));
        normalEnd = normalData + alignedSize;
        std::size_t endMisalignment = (alignedSize & TypeAlignmentMask);
        if (endMisalignment != 0)
            _alignedEnd = normalEnd + Alignment - endMisalignment;
        else
            _alignedEnd = normalEnd;

        if (oldNormalData != nullptr && oldSize > 0)
            std::memcpy(normalData, oldNormalData, std::min(newSize, oldSize) * sizeof(Type));
        rawFree(oldData, oldMapped);

        return true;
    }

    /**
     * @brief Resizes the buffer. Given that std::realloc may return either the same pointer
     * or a new one, you need to account for both cases in the code if you are using deep pointers.
     *
     * @param newSize the new buffer size in bytes
     */
    void resize(size_t newSize)
    {
        if (!resize(newSize, std::nothrow))
            throw std::bad_alloc();
    }

    /**
     * @brief Clear the buffers and frees the underlying memory
     *
     */
    void clear() noexcept
    {
        if (largerSize > 0)
            counter().bufferDeleted(largerSize * sizeof(value_type));
        _clear();
    }
    ~Buffer() noexcept
    {
        if (largerSize > 0)
            counter().bufferDeleted(largerSize * sizeof(value_type));
    }

    /**
     * @brief Get the size of the larger block which is actually allocated
     */
    size_t allocationSize() const noexcept
    {
        return largerSize;
    }

    /**
     * @brief Construct a new Buffer object from an existing one
     *
     * @param other
     */
    Buffer(const Buffer<Type, Alignment>& other)
    {
        resize(other.size());
        std::memcpy(this->data(), other.data(), other.size() * sizeof(value_type));
    }

    /**
     * @brief Construct a new Buffer object by moving an existing one
     *
     * @param other
     */
    Buffer(Buffer<Type, Alignment>&& other) noexcept
        : largerSize(other.largerSize),
          alignedSize(other.alignedSize),
          normalData(other.normalData),
          paddedData(std::move(other.paddedData)),
          normalEnd(other.normalEnd),
          _alignedEnd(other._alignedEnd)
    {
        other._clear();
    }

    Buffer<Type, Alignment>& operator=(const Buffer<Type, Alignment>& other)
    {
        if (this != &other) {
            resize(other.size());
            std::memcpy(this->data(), other.data(), other.size() * sizeof(value_type));
        }
        return *this;
    }

    Buffer<Type, Alignment>& operator=(Buffer<Type, Alignment>&& other) noexcept
    {
        if (this != &other) {
            if (largerSize > 0)
                counter().bufferDeleted(largerSize * sizeof(value_type));
            largerSize = other.largerSize;
            alignedSize = other.alignedSize;
            normalData = other.normalData;
            paddedData = std::move(other.paddedData);
            normalEnd = other.normalEnd;
            _alignedEnd = other._alignedEnd;
            other._clear();
        }
        return *this;
    }

    Type& operator[](size_t idx) noexcept { return *(normalData + idx); }
    constexpr pointer data() const noexcept { return normalData; }
    constexpr size_type size() const noexcept { return alignedSize; }
    constexpr bool empty() const noexcept { return alignedSize == 0; }
    constexpr iterator begin() const noexcept  { return data(); }
    constexpr iterator end() const noexcept { return normalEnd; }
    constexpr pointer alignedEnd() const noexcept { return _alignedEnd; }

    /**
     * @brief      Return the buffer counter object.
     *
     * @return     The buffer counter on which you can call various methods.
     */
    static BufferCounter& counter() noexcept
    {
        return BufferCounter::counter();
    }

private:
    void _clear()
    {
        largerSize = 0;
        alignedSize = 0;
        paddedData.reset();
        paddedData.get_deleter().mapped = 0;
        normalData = nullptr;
        normalEnd = nullptr;
        _alignedEnd = nullptr;
    }

private:
    static constexpr int AlignmentMask { Alignment - 1 };
    static constexpr int TypeAlignment { Alignment / sizeof(value_type) };
    static constexpr int TypeAlignmentMask { TypeAlignment - 1 };
    static_assert(std::is_trivial<value_type>::value, "Type should be trivial");
    static_assert(Alignment == 0 || Alignment == 4 || Alignment == 8 || Alignment == 16 || Alignment == 32, "Bad alignment value");
    static_assert(TypeAlignment * sizeof(value_type) == Alignment || !std::is_arithmetic<value_type>::value,
                  "The alignment does not appear to be divided by the size of the arithmetic Type");
    void* align(std::size_t alignment, std::size_t size, void *ptr, std::size_t &space)
    {
        std::uintptr_t pn = reinterpret_cast<std::uintptr_t>(ptr);
        std::uintptr_t aligned = (pn + alignment - 1) & - alignment;
        std::size_t padding = aligned - pn;
        if (space < size + padding) return nullptr;
        space -= padding;
        return reinterpret_cast<void *>(aligned);
    }

    // [sfizz for MPC OS] large buffers (decoded sample files) are mapped directly. From the heap, glibc's calloc would
    // clear every page of a freshly reused block (CPU, and the whole file's memory committed at once), and freed
    // blocks stay in the process: memory then only grows as files stream in and out. A mapping is zeroed lazily
    // by the kernel and handed back whole on free.
    static constexpr std::size_t kMapThreshold { std::size_t(256) << 10 };
    static void* rawAllocate(std::size_t bytes, std::size_t& mapped) noexcept
    {
        mapped = 0;
#if defined(__linux__)
        if (bytes >= kMapThreshold) {
            void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED)
                return nullptr;
            mapped = bytes;
            return p;
        }
#endif
        return std::calloc(bytes, 1);
    }
    static void rawFree(void* p, std::size_t mapped) noexcept
    {
        if (p == nullptr)
            return;
#if defined(__linux__)
        if (mapped > 0) {
            ::munmap(p, mapped);
            return;
        }
#endif
        std::free(p);
    }

    struct deleter {
        std::size_t mapped { 0 };
        void operator()(void *p) const noexcept { rawFree(p, mapped); }
    };

    size_type largerSize { 0 };
    size_type alignedSize { 0 };
    pointer normalData { nullptr };
    std::unique_ptr<value_type[], deleter> paddedData;
    pointer normalEnd { nullptr };
    pointer _alignedEnd { nullptr };
    LEAK_DETECTOR(Buffer);
};

}
