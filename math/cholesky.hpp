#pragma once

/*
    Cholesky decomposition of a symmetric positive-definite matrix.
    Templated on T so it works with the AAD Number type.

    Given A = L * L^T, returns L (lower triangular).
    Uses only +, -, *, / and sqrt — all supported by the AAD tape.
*/

#include "matrix.hpp"
#include <cmath>
#include <stdexcept>

namespace ActuaLib {

    template <class T>
    Matrix<T> cholesky(const Matrix<T>& A) {
        using std::sqrt;

        const size_t n = A.rows();
        if (n != A.cols()) {
            throw std::runtime_error("cholesky: matrix must be square");
        }

        Matrix<T> L(n, n);
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                L[i][j] = T(0.0);
            }
        }

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j <= i; ++j) {
                T sum = T(0.0);
                for (size_t k = 0; k < j; ++k) {
                    sum = sum + L[i][k] * L[j][k];
                }
                if (i == j) {
                    T diag = A[i][i] - sum;
                    L[i][j] = sqrt(diag);
                } else {
                    L[i][j] = (A[i][j] - sum) / L[j][j];
                }
            }
        }
        return L;
    }

} // namespace ActuaLib
