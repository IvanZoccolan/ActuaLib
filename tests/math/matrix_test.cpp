#include <gtest/gtest.h>
#include "math/matrix.hpp"
#include <cmath>

using namespace ActuaLib;

// Test fixture for Matrix tests
class MatrixTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Code here will be called immediately after the constructor
    }

    void TearDown() override {
        // Code here will be called immediately after each test
    }
};

// Test default constructor
TEST_F(MatrixTest, DefaultConstructor) {
    Matrix<double> m;
    EXPECT_EQ(m.rows(), 0);
    EXPECT_EQ(m.cols(), 0);
    EXPECT_TRUE(m.empty());
}

// Test parameterized constructor
TEST_F(MatrixTest, ParameterizedConstructor) {
    Matrix<double> m(3, 4);
    EXPECT_EQ(m.rows(), 3);
    EXPECT_EQ(m.cols(), 4);
    EXPECT_FALSE(m.empty());
}

// Test element access
TEST_F(MatrixTest, ElementAccess) {
    Matrix<double> m(2, 3);
    m[0][0] = 1.0;
    m[0][1] = 2.0;
    m[0][2] = 3.0;
    m[1][0] = 4.0;
    m[1][1] = 5.0;
    m[1][2] = 6.0;
    
    EXPECT_DOUBLE_EQ(m[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m[0][1], 2.0);
    EXPECT_DOUBLE_EQ(m[0][2], 3.0);
    EXPECT_DOUBLE_EQ(m[1][0], 4.0);
    EXPECT_DOUBLE_EQ(m[1][1], 5.0);
    EXPECT_DOUBLE_EQ(m[1][2], 6.0);
}

// Test copy constructor
TEST_F(MatrixTest, CopyConstructor) {
    Matrix<double> m1(2, 2);
    m1[0][0] = 1.0;
    m1[0][1] = 2.0;
    m1[1][0] = 3.0;
    m1[1][1] = 4.0;
    
    Matrix<double> m2(m1);
    
    EXPECT_EQ(m2.rows(), 2);
    EXPECT_EQ(m2.cols(), 2);
    EXPECT_DOUBLE_EQ(m2[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m2[0][1], 2.0);
    EXPECT_DOUBLE_EQ(m2[1][0], 3.0);
    EXPECT_DOUBLE_EQ(m2[1][1], 4.0);
    
    // Ensure deep copy
    m1[0][0] = 99.0;
    EXPECT_DOUBLE_EQ(m2[0][0], 1.0);
}

// Test copy assignment operator
TEST_F(MatrixTest, CopyAssignment) {
    Matrix<double> m1(2, 2);
    m1[0][0] = 1.0;
    m1[0][1] = 2.0;
    m1[1][0] = 3.0;
    m1[1][1] = 4.0;
    
    Matrix<double> m2;
    m2 = m1;
    
    EXPECT_EQ(m2.rows(), 2);
    EXPECT_EQ(m2.cols(), 2);
    EXPECT_DOUBLE_EQ(m2[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m2[1][1], 4.0);
    
    // Ensure deep copy
    m1[0][0] = 99.0;
    EXPECT_DOUBLE_EQ(m2[0][0], 1.0);
}

// Test self-assignment
TEST_F(MatrixTest, SelfAssignment) {
    Matrix<double> m(2, 2);
    m[0][0] = 1.0;
    m[0][1] = 2.0;
    
    m = m; // Self-assignment
    
    EXPECT_EQ(m.rows(), 2);
    EXPECT_EQ(m.cols(), 2);
    EXPECT_DOUBLE_EQ(m[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m[0][1], 2.0);
}

// Test move constructor
TEST_F(MatrixTest, MoveConstructor) {
    Matrix<double> m1(2, 2);
    m1[0][0] = 1.0;
    m1[0][1] = 2.0;
    m1[1][0] = 3.0;
    m1[1][1] = 4.0;
    
    Matrix<double> m2(std::move(m1));
    
    EXPECT_EQ(m2.rows(), 2);
    EXPECT_EQ(m2.cols(), 2);
    EXPECT_DOUBLE_EQ(m2[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m2[1][1], 4.0);
    
    // m1 should be in a valid but moved-from state
    EXPECT_EQ(m1.rows(), 0);
    EXPECT_EQ(m1.cols(), 0);
}

// Test move assignment operator
TEST_F(MatrixTest, MoveAssignment) {
    Matrix<double> m1(2, 2);
    m1[0][0] = 1.0;
    m1[0][1] = 2.0;
    m1[1][0] = 3.0;
    m1[1][1] = 4.0;
    
    Matrix<double> m2;
    m2 = std::move(m1);
    
    EXPECT_EQ(m2.rows(), 2);
    EXPECT_EQ(m2.cols(), 2);
    EXPECT_DOUBLE_EQ(m2[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m2[1][1], 4.0);
    
    // m1 should be in a valid but moved-from state
    EXPECT_EQ(m1.rows(), 0);
    EXPECT_EQ(m1.cols(), 0);
}

// Test swap
TEST_F(MatrixTest, Swap) {
    Matrix<double> m1(2, 2);
    m1[0][0] = 1.0;
    m1[1][1] = 2.0;
    
    Matrix<double> m2(3, 3);
    m2[0][0] = 10.0;
    m2[2][2] = 20.0;
    
    m1.swap(m2);
    
    EXPECT_EQ(m1.rows(), 3);
    EXPECT_EQ(m1.cols(), 3);
    EXPECT_DOUBLE_EQ(m1[0][0], 10.0);
    EXPECT_DOUBLE_EQ(m1[2][2], 20.0);
    
    EXPECT_EQ(m2.rows(), 2);
    EXPECT_EQ(m2.cols(), 2);
    EXPECT_DOUBLE_EQ(m2[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m2[1][1], 2.0);
}

// Test resize
TEST_F(MatrixTest, Resize) {
    Matrix<double> m(2, 2);
    m[0][0] = 1.0;
    m[1][1] = 2.0;
    
    m.resize(3, 4);
    
    EXPECT_EQ(m.rows(), 3);
    EXPECT_EQ(m.cols(), 4);
    EXPECT_FALSE(m.empty());
}

// Test iterators
TEST_F(MatrixTest, Iterators) {
    Matrix<double> m(2, 3);
    double value = 1.0;
    for (auto it = m.begin(); it != m.end(); ++it) {
        *it = value++;
    }
    
    EXPECT_DOUBLE_EQ(m[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m[0][1], 2.0);
    EXPECT_DOUBLE_EQ(m[0][2], 3.0);
    EXPECT_DOUBLE_EQ(m[1][0], 4.0);
    EXPECT_DOUBLE_EQ(m[1][1], 5.0);
    EXPECT_DOUBLE_EQ(m[1][2], 6.0);
}

// Test const iterators
TEST_F(MatrixTest, ConstIterators) {
    Matrix<double> m(2, 2);
    m[0][0] = 1.0;
    m[0][1] = 2.0;
    m[1][0] = 3.0;
    m[1][1] = 4.0;
    
    const Matrix<double>& const_m = m;
    
    double sum = 0.0;
    for (auto it = const_m.begin(); it != const_m.end(); ++it) {
        sum += *it;
    }
    
    EXPECT_DOUBLE_EQ(sum, 10.0);
}

// Test matrix multiplication - square matrices
TEST_F(MatrixTest, MultiplicationSquare) {
    Matrix<double> A(2, 2);
    A[0][0] = 1.0; A[0][1] = 2.0;
    A[1][0] = 3.0; A[1][1] = 4.0;
    
    Matrix<double> B(2, 2);
    B[0][0] = 5.0; B[0][1] = 6.0;
    B[1][0] = 7.0; B[1][1] = 8.0;
    
    Matrix<double> C = A * B;
    
    EXPECT_EQ(C.rows(), 2);
    EXPECT_EQ(C.cols(), 2);
    
    // Expected: [1*5+2*7, 1*6+2*8]   = [19, 22]
    //           [3*5+4*7, 3*6+4*8]   = [43, 50]
    EXPECT_DOUBLE_EQ(C[0][0], 19.0);
    EXPECT_DOUBLE_EQ(C[0][1], 22.0);
    EXPECT_DOUBLE_EQ(C[1][0], 43.0);
    EXPECT_DOUBLE_EQ(C[1][1], 50.0);
}

// Test matrix multiplication - rectangular matrices
TEST_F(MatrixTest, MultiplicationRectangular) {
    Matrix<double> A(2, 3);
    A[0][0] = 1.0; A[0][1] = 2.0; A[0][2] = 3.0;
    A[1][0] = 4.0; A[1][1] = 5.0; A[1][2] = 6.0;
    
    Matrix<double> B(3, 2);
    B[0][0] = 7.0;  B[0][1] = 8.0;
    B[1][0] = 9.0;  B[1][1] = 10.0;
    B[2][0] = 11.0; B[2][1] = 12.0;
    
    Matrix<double> C = A * B;
    
    EXPECT_EQ(C.rows(), 2);
    EXPECT_EQ(C.cols(), 2);
    
    // Expected: [1*7+2*9+3*11, 1*8+2*10+3*12]   = [58, 64]
    //           [4*7+5*9+6*11, 4*8+5*10+6*12]   = [139, 154]
    EXPECT_DOUBLE_EQ(C[0][0], 58.0);
    EXPECT_DOUBLE_EQ(C[0][1], 64.0);
    EXPECT_DOUBLE_EQ(C[1][0], 139.0);
    EXPECT_DOUBLE_EQ(C[1][1], 154.0);
}

// Test matrix multiplication - identity matrix
TEST_F(MatrixTest, MultiplicationIdentity) {
    Matrix<double> A(2, 2);
    A[0][0] = 3.0; A[0][1] = 4.0;
    A[1][0] = 5.0; A[1][1] = 6.0;
    
    Matrix<double> I(2, 2);
    I[0][0] = 1.0; I[0][1] = 0.0;
    I[1][0] = 0.0; I[1][1] = 1.0;
    
    Matrix<double> C = A * I;
    
    EXPECT_DOUBLE_EQ(C[0][0], 3.0);
    EXPECT_DOUBLE_EQ(C[0][1], 4.0);
    EXPECT_DOUBLE_EQ(C[1][0], 5.0);
    EXPECT_DOUBLE_EQ(C[1][1], 6.0);
}

// Test matrix multiplication - dimension mismatch
TEST_F(MatrixTest, MultiplicationDimensionMismatch) {
    Matrix<double> A(2, 3);
    Matrix<double> B(2, 2);  // B.rows() != A.cols()
    
    EXPECT_THROW({
        Matrix<double> C = A * B;
    }, std::runtime_error);
}

// Test transpose
TEST_F(MatrixTest, Transpose) {
    Matrix<double> M(2, 3);
    M[0][0] = 1.0; M[0][1] = 2.0; M[0][2] = 3.0;
    M[1][0] = 4.0; M[1][1] = 5.0; M[1][2] = 6.0;
    
    Matrix<double> Mt = transpose(M);
    
    EXPECT_EQ(Mt.rows(), 3);
    EXPECT_EQ(Mt.cols(), 2);
    
    EXPECT_DOUBLE_EQ(Mt[0][0], 1.0);
    EXPECT_DOUBLE_EQ(Mt[0][1], 4.0);
    EXPECT_DOUBLE_EQ(Mt[1][0], 2.0);
    EXPECT_DOUBLE_EQ(Mt[1][1], 5.0);
    EXPECT_DOUBLE_EQ(Mt[2][0], 3.0);
    EXPECT_DOUBLE_EQ(Mt[2][1], 6.0);
}

// Test transpose square matrix
TEST_F(MatrixTest, TransposeSquare) {
    Matrix<double> M(2, 2);
    M[0][0] = 1.0; M[0][1] = 2.0;
    M[1][0] = 3.0; M[1][1] = 4.0;
    
    Matrix<double> Mt = transpose(M);
    
    EXPECT_EQ(Mt.rows(), 2);
    EXPECT_EQ(Mt.cols(), 2);
    
    EXPECT_DOUBLE_EQ(Mt[0][0], 1.0);
    EXPECT_DOUBLE_EQ(Mt[0][1], 3.0);
    EXPECT_DOUBLE_EQ(Mt[1][0], 2.0);
    EXPECT_DOUBLE_EQ(Mt[1][1], 4.0);
}

// Test transpose of transpose
TEST_F(MatrixTest, DoubleTranspose) {
    Matrix<double> M(2, 3);
    M[0][0] = 1.0; M[0][1] = 2.0; M[0][2] = 3.0;
    M[1][0] = 4.0; M[1][1] = 5.0; M[1][2] = 6.0;
    
    Matrix<double> Mt = transpose(transpose(M));
    
    EXPECT_EQ(Mt.rows(), 2);
    EXPECT_EQ(Mt.cols(), 3);
    
    for (size_t i = 0; i < M.rows(); ++i) {
        for (size_t j = 0; j < M.cols(); ++j) {
            EXPECT_DOUBLE_EQ(Mt[i][j], M[i][j]);
        }
    }
}

// Test type conversion constructor
TEST_F(MatrixTest, TypeConversionConstructor) {
    Matrix<int> mi(2, 2);
    mi[0][0] = 1; mi[0][1] = 2;
    mi[1][0] = 3; mi[1][1] = 4;
    
    Matrix<double> md(mi);
    
    EXPECT_EQ(md.rows(), 2);
    EXPECT_EQ(md.cols(), 2);
    EXPECT_DOUBLE_EQ(md[0][0], 1.0);
    EXPECT_DOUBLE_EQ(md[0][1], 2.0);
    EXPECT_DOUBLE_EQ(md[1][0], 3.0);
    EXPECT_DOUBLE_EQ(md[1][1], 4.0);
}

// Test large matrix multiplication (stress test)
TEST_F(MatrixTest, LargeMatrixMultiplication) {
    const size_t N = 100;
    Matrix<double> A(N, N);
    Matrix<double> B(N, N);
    
    // Fill with simple values
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = 0; j < N; ++j) {
            A[i][j] = static_cast<double>(i + j);
            B[i][j] = static_cast<double>(i * j);
        }
    }
    
    Matrix<double> C = A * B;
    
    EXPECT_EQ(C.rows(), N);
    EXPECT_EQ(C.cols(), N);
    EXPECT_FALSE(C.empty());
}

// Main function
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
