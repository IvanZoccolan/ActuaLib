#pragma once

#include <cassert>
#include <cstddef>

#include "gradient_tape.hpp"

namespace ActuaLib {

template <class T>
class ad_scalar {
public:
    ad_scalar() = default;

    explicit ad_scalar(const T& value)
        : value_(value) {}

    ad_scalar(gradient_tape& tape, const T& value)
        : value_(value)
        , tape_(&tape)
        , slot_(tape.record_leaf()) {}

    static ad_scalar<T> from_recorded(const T& value, gradient_tape* tape, const std::size_t slot) {
        ad_scalar<T> out;
        out.value_ = value;
        out.tape_ = tape;
        out.slot_ = slot;
        return out;
    }

    T& value() { return value_; }
    const T& value() const { return value_; }

    bool active() const { return tape_ != nullptr && slot_ != gradient_tape::npos; }
    std::size_t slot() const { return slot_; }
    gradient_tape* tape() const { return tape_; }

    double adjoint() const {
        return active() ? tape_->adjoint(slot_) : 0.0;
    }

private:
    template <class U>
    friend ad_scalar<U> operator+(const ad_scalar<U>& lhs, const ad_scalar<U>& rhs);
    template <class U>
    friend ad_scalar<U> operator-(const ad_scalar<U>& lhs, const ad_scalar<U>& rhs);
    template <class U>
    friend ad_scalar<U> operator*(const ad_scalar<U>& lhs, const ad_scalar<U>& rhs);
    template <class U>
    friend ad_scalar<U> operator/(const ad_scalar<U>& lhs, const ad_scalar<U>& rhs);

    T value_{};
    gradient_tape* tape_ = nullptr;
    std::size_t slot_ = gradient_tape::npos;
};

template <class T>
inline gradient_tape* choose_tape(const ad_scalar<T>& lhs, const ad_scalar<T>& rhs) {
    if (!lhs.active()) {
        return rhs.tape();
    }
    if (!rhs.active()) {
        return lhs.tape();
    }
    assert(lhs.tape() == rhs.tape() && "AAD expression combines two different tapes");
    return lhs.tape();
}

template <class T>
ad_scalar<T> operator+(const ad_scalar<T>& lhs, const ad_scalar<T>& rhs) {
    const T v = lhs.value_ + rhs.value_;
    gradient_tape* tape = choose_tape(lhs, rhs);
    if (tape == nullptr) {
        return ad_scalar<T>(v);
    }

    if (lhs.active() && rhs.active()) {
        const std::size_t slot = tape->record_binary(lhs.slot_, rhs.slot_, 1.0, 1.0);
        return ad_scalar<T>::from_recorded(v, tape, slot);
    }
    if (lhs.active()) {
        const std::size_t slot = tape->record_unary(lhs.slot_, 1.0);
        return ad_scalar<T>::from_recorded(v, tape, slot);
    }
    const std::size_t slot = tape->record_unary(rhs.slot_, 1.0);
    return ad_scalar<T>::from_recorded(v, tape, slot);
}

template <class T>
ad_scalar<T> operator-(const ad_scalar<T>& lhs, const ad_scalar<T>& rhs) {
    const T v = lhs.value_ - rhs.value_;
    gradient_tape* tape = choose_tape(lhs, rhs);
    if (tape == nullptr) {
        return ad_scalar<T>(v);
    }

    if (lhs.active() && rhs.active()) {
        const std::size_t slot = tape->record_binary(lhs.slot_, rhs.slot_, 1.0, -1.0);
        return ad_scalar<T>::from_recorded(v, tape, slot);
    }
    if (lhs.active()) {
        const std::size_t slot = tape->record_unary(lhs.slot_, 1.0);
        return ad_scalar<T>::from_recorded(v, tape, slot);
    }
    const std::size_t slot = tape->record_unary(rhs.slot_, -1.0);
    return ad_scalar<T>::from_recorded(v, tape, slot);
}

template <class T>
ad_scalar<T> operator*(const ad_scalar<T>& lhs, const ad_scalar<T>& rhs) {
    const T v = lhs.value_ * rhs.value_;
    gradient_tape* tape = choose_tape(lhs, rhs);
    if (tape == nullptr) {
        return ad_scalar<T>(v);
    }

    if (lhs.active() && rhs.active()) {
        const std::size_t slot = tape->record_binary(lhs.slot_, rhs.slot_, rhs.value_, lhs.value_);
        return ad_scalar<T>::from_recorded(v, tape, slot);
    }
    if (lhs.active()) {
        const std::size_t slot = tape->record_unary(lhs.slot_, rhs.value_);
        return ad_scalar<T>::from_recorded(v, tape, slot);
    }
    const std::size_t slot = tape->record_unary(rhs.slot_, lhs.value_);
    return ad_scalar<T>::from_recorded(v, tape, slot);
}

template <class T>
ad_scalar<T> operator/(const ad_scalar<T>& lhs, const ad_scalar<T>& rhs) {
    const T v = lhs.value_ / rhs.value_;
    gradient_tape* tape = choose_tape(lhs, rhs);
    if (tape == nullptr) {
        return ad_scalar<T>(v);
    }

    if (lhs.active() && rhs.active()) {
        const T inv = T(1) / rhs.value_;
        const T d_rhs = -lhs.value_ * inv * inv;
        const std::size_t slot = tape->record_binary(lhs.slot_, rhs.slot_, inv, d_rhs);
        return ad_scalar<T>::from_recorded(v, tape, slot);
    }
    if (lhs.active()) {
        const T inv = T(1) / rhs.value_;
        const std::size_t slot = tape->record_unary(lhs.slot_, inv);
        return ad_scalar<T>::from_recorded(v, tape, slot);
    }

    const T d_rhs = -lhs.value_ / (rhs.value_ * rhs.value_);
    const std::size_t slot = tape->record_unary(rhs.slot_, d_rhs);
    return ad_scalar<T>::from_recorded(v, tape, slot);
}

} // namespace ActuaLib
