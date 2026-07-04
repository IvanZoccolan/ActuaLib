/*
 * Adapted from: A. Savine, "Modern Computational Finance: AAD and Parallel
 * Simulations", Wiley, 2018. Used and modified under the book's license.
 * Operator set and math overloads adapted; internal representation replaced
 * (slot-index ad_scalar vs pointer-based Node*).
 */

#pragma once

#include <cmath>
#include <memory>
#include <stdexcept>

#include "ad_scalar.hpp"
#include "gradient_tape.hpp"
#include "../math/randomnumbers/gaussians.hpp"

namespace ActuaLib {

class Number;
using Tape = gradient_tape;

struct Node {
    static size_t numAdj;
};

class Number {
public:
    static thread_local Tape* tape;

    Number() = default;

    explicit Number(const double value)
        : scalar_(value) {}

    Number& operator=(const double value) {
        scalar_ = active_scalar(value);
        return *this;
    }

    void putOnTape() {
        if (tape == nullptr) {
            throw std::runtime_error("No active gradient tape");
        }
        scalar_ = ad_scalar<double>(*tape, scalar_.value());
    }

    explicit operator double&() {
        return scalar_.value();
    }

    explicit operator double() const {
        return scalar_.value();
    }

    double& value() {
        return scalar_.value();
    }

    double value() const {
        return scalar_.value();
    }

    double& adjoint() {
        ensure_active();
        return scalar_.tape()->adjoint(scalar_.slot());
    }

    double adjoint() const {
        return scalar_.adjoint();
    }

    double& adjoint(const size_t) {
        return adjoint();
    }

    double adjoint(const size_t) const {
        return adjoint();
    }

    void resetAdjoints() {
        if (tape == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < tape->size(); ++i) {
            tape->adjoint(i) = 0.0;
        }
    }

    void propagateToStart() {
        ensure_active();
        scalar_.tape()->reverse_from(scalar_.slot());
    }

    void propagateToMark() {
        ensure_active();
        scalar_.tape()->reverse_to_mark(scalar_.slot());
    }

    static void propagateMarkToStart() {
        if (tape == nullptr) {
            return;
        }
        tape->reverse_mark_to_start();
    }

    Number& operator+=(const Number& arg) {
        *this = *this + arg;
        return *this;
    }

    Number& operator+=(const double& arg) {
        *this = *this + arg;
        return *this;
    }

    Number& operator-=(const Number& arg) {
        *this = *this - arg;
        return *this;
    }

    Number& operator-=(const double& arg) {
        *this = *this - arg;
        return *this;
    }

    Number& operator*=(const Number& arg) {
        *this = *this * arg;
        return *this;
    }

    Number& operator*=(const double& arg) {
        *this = *this * arg;
        return *this;
    }

    Number& operator/=(const Number& arg) {
        *this = *this / arg;
        return *this;
    }

    Number& operator/=(const double& arg) {
        *this = *this / arg;
        return *this;
    }

    Number operator-() const {
        return 0.0 - *this;
    }

    Number operator+() const {
        return *this;
    }

    friend Number operator+(const Number& lhs, const Number& rhs) {
        return from_scalar(lhs.scalar_ + rhs.scalar_);
    }

    friend Number operator+(const Number& lhs, const double& rhs) {
        return lhs + Number(rhs);
    }

    friend Number operator+(const double& lhs, const Number& rhs) {
        return Number(lhs) + rhs;
    }

    friend Number operator-(const Number& lhs, const Number& rhs) {
        return from_scalar(lhs.scalar_ - rhs.scalar_);
    }

    friend Number operator-(const Number& lhs, const double& rhs) {
        return lhs - Number(rhs);
    }

    friend Number operator-(const double& lhs, const Number& rhs) {
        return Number(lhs) - rhs;
    }

    friend Number operator*(const Number& lhs, const Number& rhs) {
        return from_scalar(lhs.scalar_ * rhs.scalar_);
    }

    friend Number operator*(const Number& lhs, const double& rhs) {
        return lhs * Number(rhs);
    }

    friend Number operator*(const double& lhs, const Number& rhs) {
        return Number(lhs) * rhs;
    }

    friend Number operator/(const Number& lhs, const Number& rhs) {
        return from_scalar(lhs.scalar_ / rhs.scalar_);
    }

    friend Number operator/(const Number& lhs, const double& rhs) {
        return lhs / Number(rhs);
    }

    friend Number operator/(const double& lhs, const Number& rhs) {
        return Number(lhs) / rhs;
    }

    friend Number pow(const Number& lhs, const Number& rhs) {
        const double value = std::pow(lhs.value(), rhs.value());
        const auto* tape_ptr = choose_tape(lhs.scalar_, rhs.scalar_);
        if (tape_ptr == nullptr) {
            return Number(value);
        }

        const double d_lhs = rhs.value() * value / lhs.value();
        const double d_rhs = std::log(lhs.value()) * value;
        return record_binary(lhs, rhs, value, d_lhs, d_rhs);
    }

    friend Number pow(const Number& lhs, const double& rhs) {
        const double value = std::pow(lhs.value(), rhs);
        if (!lhs.scalar_.active()) {
            return Number(value);
        }
        return record_unary(lhs, value, rhs * value / lhs.value());
    }

    friend Number pow(const double& lhs, const Number& rhs) {
        const double value = std::pow(lhs, rhs.value());
        if (!rhs.scalar_.active()) {
            return Number(value);
        }
        return record_unary(rhs, value, std::log(lhs) * value);
    }

    friend Number max(const Number& lhs, const Number& rhs) {
        return lhs.value() > rhs.value()
            ? select_binary(lhs, rhs, lhs.value(), 1.0, 0.0)
            : select_binary(lhs, rhs, rhs.value(), 0.0, 1.0);
    }

    friend Number max(const Number& lhs, const double& rhs) {
        return lhs.value() > rhs ? select_unary(lhs, lhs.value(), 1.0) : Number(rhs);
    }

    friend Number max(const double& lhs, const Number& rhs) {
        return lhs > rhs.value() ? Number(lhs) : select_unary(rhs, rhs.value(), 1.0);
    }

    friend Number min(const Number& lhs, const Number& rhs) {
        return lhs.value() < rhs.value()
            ? select_binary(lhs, rhs, lhs.value(), 1.0, 0.0)
            : select_binary(lhs, rhs, rhs.value(), 0.0, 1.0);
    }

    friend Number min(const Number& lhs, const double& rhs) {
        return lhs.value() < rhs ? select_unary(lhs, lhs.value(), 1.0) : Number(rhs);
    }

    friend Number min(const double& lhs, const Number& rhs) {
        return lhs < rhs.value() ? Number(lhs) : select_unary(rhs, rhs.value(), 1.0);
    }

    friend Number exp(const Number& arg) {
        const double value = std::exp(arg.value());
        return arg.scalar_.active() ? record_unary(arg, value, value) : Number(value);
    }

    friend Number log(const Number& arg) {
        const double value = std::log(arg.value());
        return arg.scalar_.active() ? record_unary(arg, value, 1.0 / arg.value()) : Number(value);
    }

    friend Number sqrt(const Number& arg) {
        const double value = std::sqrt(arg.value());
        return arg.scalar_.active() ? record_unary(arg, value, 0.5 / value) : Number(value);
    }

    friend Number fabs(const Number& arg) {
        const double value = std::fabs(arg.value());
        const double deriv = arg.value() > 0.0 ? 1.0 : -1.0;
        return arg.scalar_.active() ? record_unary(arg, value, deriv) : Number(value);
    }

    friend Number normalDens(const Number& arg) {
        const double value = ActuaLib::normalDens(arg.value());
        return arg.scalar_.active() ? record_unary(arg, value, -arg.value() * value) : Number(value);
    }

    friend Number normalCdf(const Number& arg) {
        const double value = ActuaLib::normalCdf(arg.value());
        return arg.scalar_.active() ? record_unary(arg, value, ActuaLib::normalDens(arg.value())) : Number(value);
    }

    friend bool operator==(const Number& lhs, const Number& rhs) { return lhs.value() == rhs.value(); }
    friend bool operator==(const Number& lhs, const double& rhs) { return lhs.value() == rhs; }
    friend bool operator==(const double& lhs, const Number& rhs) { return lhs == rhs.value(); }
    friend bool operator!=(const Number& lhs, const Number& rhs) { return lhs.value() != rhs.value(); }
    friend bool operator!=(const Number& lhs, const double& rhs) { return lhs.value() != rhs; }
    friend bool operator!=(const double& lhs, const Number& rhs) { return lhs != rhs.value(); }
    friend bool operator<(const Number& lhs, const Number& rhs) { return lhs.value() < rhs.value(); }
    friend bool operator<(const Number& lhs, const double& rhs) { return lhs.value() < rhs; }
    friend bool operator<(const double& lhs, const Number& rhs) { return lhs < rhs.value(); }
    friend bool operator>(const Number& lhs, const Number& rhs) { return lhs.value() > rhs.value(); }
    friend bool operator>(const Number& lhs, const double& rhs) { return lhs.value() > rhs; }
    friend bool operator>(const double& lhs, const Number& rhs) { return lhs > rhs.value(); }
    friend bool operator<=(const Number& lhs, const Number& rhs) { return lhs.value() <= rhs.value(); }
    friend bool operator<=(const Number& lhs, const double& rhs) { return lhs.value() <= rhs; }
    friend bool operator<=(const double& lhs, const Number& rhs) { return lhs <= rhs.value(); }
    friend bool operator>=(const Number& lhs, const Number& rhs) { return lhs.value() >= rhs.value(); }
    friend bool operator>=(const Number& lhs, const double& rhs) { return lhs.value() >= rhs; }
    friend bool operator>=(const double& lhs, const Number& rhs) { return lhs >= rhs.value(); }

private:
    explicit Number(const ad_scalar<double>& scalar)
        : scalar_(scalar) {}

    static Number from_scalar(const ad_scalar<double>& scalar) {
        return Number(scalar);
    }

    static ad_scalar<double> active_scalar(const double value) {
        return tape == nullptr ? ad_scalar<double>(value) : ad_scalar<double>(*tape, value);
    }

    void ensure_active() const {
        if (!scalar_.active()) {
            throw std::runtime_error("Requested adjoint on inactive number");
        }
    }

    static Number record_unary(const Number& arg, const double value, const double derivative) {
        const std::size_t slot = arg.scalar_.tape()->record_unary(arg.scalar_.slot(), derivative);
        return Number(ad_scalar<double>::from_recorded(value, arg.scalar_.tape(), slot));
    }

    static Number record_binary(const Number& lhs, const Number& rhs,
        const double value, const double left_derivative, const double right_derivative) {
        auto* tape_ptr = choose_tape(lhs.scalar_, rhs.scalar_);
        const bool lhs_active = lhs.scalar_.active();
        const bool rhs_active = rhs.scalar_.active();
        if (tape_ptr == nullptr) {
            return Number(value);
        }
        if (lhs_active && rhs_active) {
            const std::size_t slot = tape_ptr->record_binary(
                lhs.scalar_.slot(), rhs.scalar_.slot(), left_derivative, right_derivative);
            return Number(ad_scalar<double>::from_recorded(value, tape_ptr, slot));
        }
        if (lhs_active) {
            const std::size_t slot = tape_ptr->record_unary(lhs.scalar_.slot(), left_derivative);
            return Number(ad_scalar<double>::from_recorded(value, tape_ptr, slot));
        }
        const std::size_t slot = tape_ptr->record_unary(rhs.scalar_.slot(), right_derivative);
        return Number(ad_scalar<double>::from_recorded(value, tape_ptr, slot));
    }

    static Number select_unary(const Number& arg, const double value, const double derivative) {
        return arg.scalar_.active() ? record_unary(arg, value, derivative) : Number(value);
    }

    static Number select_binary(const Number& lhs, const Number& rhs,
        const double value, const double left_derivative, const double right_derivative) {
        return record_binary(lhs, rhs, value, left_derivative, right_derivative);
    }

    ad_scalar<double> scalar_;
};

} // namespace ActuaLib
