#pragma once

#include "front_back_builder.hpp"

namespace taffy {

template <class T>
void FrontBackVecBuilder<T>::push_front(T value) {
    check_live();
    if (front_ >= slots_.size())
        panic("front builder index out of bounds");
    slots_[front_].emplace(std::move(value));
    ++front_;
}

template <class T>
void FrontBackVecBuilder<T>::push_back(T value) {
    check_live();
    if (back_ >= slots_.size())
        panic("back builder index out of bounds");
    slots_[slots_.size() - back_ - 1].emplace(std::move(value));
    ++back_;
}

template <class T>
std::pair<std::vector<T>, std::size_t> FrontBackVecBuilder<T>::into_vec() {
    check_live();
    if (front_ > slots_.size() || back_ != slots_.size() - front_)
        panic("the number of items pushed to a FrontBackVecBuilder must equal its capacity");
    std::vector<T> result;
    result.reserve(slots_.size());
    for (auto& slot : slots_)
        result.push_back(std::move(*slot));
    poisoned_ = true; // consuming operation, including an empty builder
    slots_.clear();
    return {std::move(result), front_};
}

template <class T>
[[noreturn]] void FrontBackVecBuilder<T>::panic(const char* message) {
    poisoned_ = true;
    throw Panic(message);
}

template <class T>
void FrontBackVecBuilder<T>::check_live() {
    if (poisoned_)
        throw Panic("invalidated FrontBackVecBuilder");
}

} // namespace taffy
