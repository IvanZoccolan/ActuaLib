#pragma once

#include <cstddef>
#include <vector>
#include <utility>
#include <stdexcept>
#include <algorithm>

namespace ActuaLib {
template <class T>
    class Matrix {
        private:

            size_t rows_;
            size_t cols_;

            std::vector<T> data_;

        public:
            Matrix() : rows_(0), cols_(0), data_() {}
            Matrix(const size_t r, const size_t c) : rows_(r), cols_(c), data_(r * c) {}

            Matrix(const Matrix& rhs)
                : rows_(rhs.rows_), cols_(rhs.cols_), data_(rhs.data_) {}
            
            Matrix& operator=(const Matrix& rhs) {
                if (this != &rhs) {
                    Matrix<T> temp(rhs);
                    swap(temp);
                }
                return *this;
            }
            
            Matrix(Matrix&& rhs) noexcept
                : rows_(rhs.rows_), cols_(rhs.cols_), data_(std::move(rhs.data_)) {
                rhs.rows_ = 0;
                rhs.cols_ = 0;
            }

            Matrix& operator=(Matrix&& rhs) noexcept {
                if (this != &rhs) {
                    rows_ = rhs.rows_;
                    cols_ = rhs.cols_;
                    data_ = std::move(rhs.data_);
                    rhs.rows_ = 0;
                    rhs.cols_ = 0;
                }
                return *this;
            }

            // Copy, assign from different (convertible) type
            template <class U>
            Matrix(const Matrix<U>& rhs) : rows_(rhs.rows()), cols_(rhs.cols())  {
                data_.resize(rows_ * cols_);
                std::copy(rhs.begin(), rhs.end(), data_.begin());
            }

            template <class U>
            Matrix& operator=(const Matrix<U>& rhs) {
                Matrix<T> temp(rhs);
                swap(temp);
                return *this;
            }

            // Swapper
            void swap(Matrix &rhs) noexcept {
                std::swap(rows_, rhs.rows_);
                std::swap(cols_, rhs.cols_);
                data_.swap(rhs.data_);   
            }

            // Resizer 

            void resize(const size_t r, const size_t c) {
                rows_ = r;
                cols_ = c;
                data_.resize(r * c);
            }

            // Access operators

            T* operator[] (const size_t r) {
                return data_.data() + r * cols_;
            }
            const T* operator[] (const size_t r) const {
                return data_.data() + r * cols_;
            }

            size_t rows() const { return rows_; }
            size_t cols() const { return cols_; }

                bool empty() const { return data_.empty(); }

            // Iterators
            using iterator = typename std::vector<T>::iterator;
            using const_iterator = typename std::vector<T>::const_iterator;
                iterator begin() { return data_.begin(); }
                iterator end() { return data_.end(); }
                const_iterator begin() const { return data_.begin(); }
                const_iterator end() const { return data_.end(); }
    };

    // Matrix multiplication

    inline Matrix<double> operator*(const Matrix<double>& A, const Matrix<double>& B) {
        if (A.cols() != B.rows()) {
            throw std::runtime_error("Matrix dimensions do not match for multiplication.");
        }


        Matrix<double> C(A.rows(), B.cols());
        std::fill(C.begin(), C.end(), 0.0);

#ifdef _OPENMP
        #pragma omp parallel for
#endif
        for (size_t i = 0; i < A.rows(); ++i) {
            const auto Ai = A[i];
            auto Ci = C[i];
            for (size_t k = 0; k < A.cols(); ++k) {
                const auto Bk = B[k];
                const auto aik = Ai[k];
                for (size_t j = 0; j < B.cols(); ++j) {
                    Ci[j] += aik * Bk[j];
                }
            }
        }
        return C;
    }

   // Matrix transpose
   template <class T>
   Matrix<T> transpose(const Matrix<T>& M) {
        Matrix<T> Mt(M.cols(), M.rows());
        for (size_t i = 0; i < M.rows(); ++i) {
            for (size_t j = 0; j < M.cols(); ++j) {
                Mt[j][i] = M[i][j];
            }
        }
        return Mt;
    } 

} // namespace ActuaLib
