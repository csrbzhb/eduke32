
#pragma once

#ifndef collections_h_
#define collections_h_

#include "compat.h"

#ifdef HAVE_CXX11_HEADERS

// GrowArray - heap-allocated storage that can expand at runtime
// requirements: type must work properly with realloc -- otherwise, use std::vector

template <typename T, size_t increment_ = 1, typename = enable_if_t<std::is_standard_layout<T>::value>, typename = enable_if_t<std::is_trivial<T>::value>, typename = enable_if_t<(increment_ > 0)>>
struct GrowArray
{
    FORCE_INLINE T * begin() const { return data_; }
    FORCE_INLINE T * end() const { return data_ + size_; }

    FORCE_INLINE size_t size() const { return size_; }

    FORCE_INLINE T& operator[](size_t index) { return data_[index]; }
    FORCE_INLINE const T& operator[](size_t index) const { return data_[index]; }

    FORCE_INLINE T& first() { return data_[0]; }
    FORCE_INLINE const T& first() const { return data_[0]; }

    FORCE_INLINE T& last() { return data_[size_-1]; }
    FORCE_INLINE const T& last() const { return data_[size_-1]; }

    void append(T item)
    {
        if (size_ == capacity_)
            reallocate(capacity_ + increment_);
        data_[size_++] = item;
    }

    void removeLast()
    {
        --size_;
    }

    void vacuum()
    {
        if (size_ < capacity_)
            reallocate(size_);
    }

    void clear()
    {
        size_ = 0;
        capacity_ = 0;
        DO_FREE_AND_NULL(data_);
    }

    // std::vector polyfills

    FORCE_INLINE T& at(size_t index) { return data_[index]; }
    FORCE_INLINE const T& at(size_t index) const { return data_[index]; }

    FORCE_INLINE void push_back(T item) { append(item); }

    void resize(size_t const newcapacity)
    {
        size_t const oldcapacity = capacity_;
        reallocate(newcapacity);
        if (newcapacity > oldcapacity)
            memset(data_ + oldcapacity, 0, (newcapacity - oldcapacity) * sizeof(T));
    }

protected:
    void reallocate(size_t newcapacity)
    {
        data_ = (T *)Xrealloc(data_, newcapacity * sizeof(T));
        capacity_ = newcapacity;
    }
    T * data_ = nullptr;
    size_t size_ = 0, capacity_ = 0;
};

#endif

#endif // collections_h_
