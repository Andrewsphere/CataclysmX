#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

namespace cataclysmx {

// Small immutable-content lists: initialized trivial values, no allocator or
// uninitialized-storage machinery. Capacity overflow is a loader error.
template<class T, std::size_t capacity>
class StaticList {
    static_assert(capacity > 0 && capacity <= 255);
    static_assert(std::is_trivially_copyable_v<T>);
public:
    constexpr std::size_t Size() const { return size_; }
    constexpr bool Empty() const { return size_ == 0; }
    constexpr bool PushBack(T value) {
        if (size_ == capacity) { return false; }
        data_[size_++] = value;
        return true;
    }
    constexpr const T& operator[](std::size_t index) const {
        if (index >= size_) { std::abort(); }
        return data_[index];
    }
    constexpr T& operator[](std::size_t index) {
        if (index >= size_) { std::abort(); }
        return data_[index];
    }
    constexpr const T* begin() const { return data_; }
    constexpr const T* end() const { return data_ + size_; }
    constexpr T* begin() { return data_; }
    constexpr T* end() { return data_ + size_; }
    constexpr bool operator==(const StaticList& other) const {
        if (size_ != other.size_) { return false; }
        for (std::size_t i = 0; i < size_; ++i) {
            if (!(data_[i] == other.data_[i])) { return false; }
        }
        return true;
    }
private:
    T data_[capacity]{};
    std::uint8_t size_ = 0;
};

}
