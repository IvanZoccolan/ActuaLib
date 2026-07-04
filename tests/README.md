# ActuaLib Tests

This directory contains the test suite for ActuaLib, organized by module.

## Structure

```
tests/
├── CMakeLists.txt          # Main tests configuration
└── math/
    ├── CMakeLists.txt      # Math module tests configuration
    └── matrix_test.cpp     # Matrix class unit tests
```

## Building Tests

Tests are built automatically when `AL_BUILD_TEST_SUITE` is ON (default).

```bash
cmake -DCMAKE_BUILD_TYPE=Release .
make
```

To disable tests:
```bash
cmake -DAL_BUILD_TEST_SUITE=OFF .
```

## Running Tests

### Run all tests with CTest:
```bash
ctest --output-on-failure
```

### Run specific test executable:
```bash
./tests/math/matrix_test
```

### Run with verbose output:
```bash
ctest -V
```

### Run specific test by name:
```bash
ctest -R MatrixTest
```

## Test Coverage

### Matrix Tests (`matrix_test.cpp`)
- **Constructors**: Default, parameterized, copy, move
- **Assignment**: Copy assignment, move assignment, self-assignment
- **Memory Operations**: Swap, resize
- **Element Access**: Operator[], iterators, const iterators
- **Operations**: Matrix multiplication, transpose
- **Type Conversion**: Cross-type construction and assignment
- **Edge Cases**: Dimension mismatch, empty matrices
- **Performance**: Large matrix stress test (100x100)

### VA Rider Tests (`testRiders.cpp`)
End-to-end validation of all VA product types against Gan's VAMC reference
implementation (datasets/Greek.csv). Prices 14 representative policies
(one per rider type) using 100K Sobol quasi-MC paths and compares to Gan's
published FMVs.

**Riders tested:**
| Rider | Policy | Our FMV | Gan FMV | Diff% | Notes |
|-------|--------|---------|---------|-------|-------|
| DBRP | 70001 | -2,290 | -2,300 | -0.4% | Excellent |
| DBSU | 130001 | -1,566 | -8,623 | -81.8% | Gan bug: DA zeroed |
| MBRP | 20001 | 49,176 | 46,158 | 6.5% | |
| MBSU | 80001 | 70,942 | 66,429 | 6.8% | |
| ABRP | 1 | 18,280 | 16,763 | 9.0% | |
| ABSU | 140001 | 142,132 | 137,104 | 3.7% | |
| WBRP | 40001 | 74,197 | 73,048 | 1.6% | Excellent |
| WBSU | 50001 | -11,677 | -11,516 | 1.4% | Excellent |
| IBRP | 90001 | 105,258 | 96,784 | 8.8% | |
| IBSU | 180001 | 90,856 | 109,446 | -17.0% | Gan bug: fund fee no *dT |
| DBMB | 160001 | 9,577 | 9,039 | 6.0% | |
| DBAB | 30001 | 39,677 | 37,740 | 5.1% | |
| DBWB | 100001 | 75,323 | 74,358 | 1.3% | Excellent |
| DBIB | 110001 | 76,487 | 70,861 | 7.9% | |

**Known bugs in Gan's code (confirmed by source inspection):**
- `PricerDBSU.java` / `PricerDBRU.java`: Death benefit amount `DA` is
  computed then immediately overwritten to 0.0 on the next line.
- `PricerIBSU.java`: Fund fee applied as `(1 - fundFee)` instead of
  `(1 - fundFee * dT)`, draining funds 12× too fast.

**Systematic bias sources (3–9% for non-buggy riders):**
1. Yield curve: Gan bootstraps with semi-annual coupons and actual
   business-day dates (ACT/ACT); we use simplified annual bootstrap.
2. MC noise: Gan uses ~1000 pseudo-random scenarios; we use 100K Sobol.
3. GMIB annuity factor: different extrapolation methodologies.

Run with:
```bash
./tests/TestRiders
```

## Adding New Tests

### 1. Create test file in appropriate module directory:
```bash
touch tests/module_name/new_test.cpp
```

### 2. Add test executable to module's CMakeLists.txt:
```cmake
add_executable(new_test new_test.cpp)

target_include_directories(new_test PRIVATE 
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/module_name"
)

target_link_libraries(new_test 
    gtest 
    gtest_main
    module_name
)

# Register with CTest
add_test(NAME NewTest COMMAND new_test)
```

### 3. Write tests using Google Test framework:
```cpp
#include <gtest/gtest.h>
#include "module_name/your_class.hpp"

TEST(TestSuiteName, TestName) {
    // Your test code
    EXPECT_EQ(actual, expected);
}
```

## Google Test Features Used

- `TEST_F`: Test fixture for setup/teardown
- `EXPECT_EQ`: Equality assertion
- `EXPECT_DOUBLE_EQ`: Floating-point equality
- `EXPECT_THROW`: Exception testing
- `EXPECT_TRUE/FALSE`: Boolean assertions

## Performance Notes

- Tests are compiled with the same optimization flags as the main library
- OpenMP is enabled for parallel tests
- ARM NEON SIMD optimizations are active on Apple Silicon
- Release builds use `-O3 -march=native`

## CI/CD Integration

Tests can be integrated into CI pipelines:
```bash
cmake -DCMAKE_BUILD_TYPE=Release .
make
ctest --output-on-failure
```

Exit code 0 indicates all tests passed.
