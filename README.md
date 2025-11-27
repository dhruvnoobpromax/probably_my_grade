# Memory Allocator Implementation

This project implements 5 different memory allocation strategies with mmap-based arena management.

## Allocators Implemented

1. **First-Fit** - O(n) - Uses linked list, allocates first block that fits
2. **Next-Fit** - O(n) - Like first-fit but continues from last allocation point
3. **Best-Fit** - O(log n) - Uses AVL tree, finds smallest block that fits
4. **Worst-Fit** - O(log n) - Uses AVL tree, finds largest block that fits
5. **Buddy** - O(log n) - Independent buddy allocator with power-of-2 blocks

## File Structure

### Core Implementation
- `2022MT11172mmu.h` - Main allocator implementation (all 5 strategies)

### Test Files
- `test_comprehensive.c` - Tests all 5 allocators with 7 test cases each
- `test_avl_complexity.c` - Verifies O(log n) complexity for Best/Worst-Fit
- `test_all_allocators.c` - Process-isolated testing (fork-based)
- `main.c` - Buddy allocator comprehensive test suite (10 tests)

### Build & Documentation
- `build_and_test.sh` - Automated build and test script
- `README.md` - This file

## Quick Start

### One-Command Testing
```bash
./build_and_test.sh
```

This will:
- ✅ Compile all tests
- ✅ Run comprehensive test (35 tests total: 5 allocators × 7 tests)
- ✅ Verify O(log n) complexity for AVL-based allocators
- ✅ Run process-isolated tests
- ✅ Test buddy allocator (10 tests)

## Manual Compilation

```bash
# Comprehensive test (all 5 allocators)
gcc -Wall -g -o test_comprehensive test_comprehensive.c -lm

# AVL complexity verification
gcc -Wall -g -o test_avl_complexity test_avl_complexity.c -lm

# Process-isolated test
gcc -Wall -g -o test_all test_all_allocators.c -lm

# Buddy allocator test
gcc -Wall -g -o main_test main.c -lm
```

## Running Individual Tests

```bash
# Test all allocators comprehensively
./test_comprehensive

# Verify O(log n) complexity
./test_avl_complexity

# Process-isolated testing
./test_all

# Buddy allocator only
./main_test
```

## Implementation Details

### Arena Management
- Uses `mmap()` for memory allocation
- Default arena size: 1MB (configurable via `ARENA_MIN`)
- Arenas are chained for expansion

### AVL Tree (Best/Worst Fit)
- Self-balancing binary search tree
- Guarantees O(log n) height
- Nodes sorted by (size, address) for deterministic behavior

### Buddy Allocator
- Independent 4MB memory pool
- Power-of-2 block sizes
- Automatic splitting and coalescing

### Alignment
- All allocations aligned to 16 bytes (configurable via `ALIGN`)

## Test Coverage

### 1. Comprehensive Test (`test_comprehensive.c`)
Tests all 5 allocators with 7 test cases each:

1. **Basic Allocations** - Verify allocation works for various sizes
2. **Alignment Check** - Ensure proper 16-byte memory alignment
3. **Free and Reuse** - Test space reuse after freeing
4. **Coalescing** - Verify adjacent free blocks merge correctly
5. **Large Allocation** - Test allocations >100KB
6. **Fragmentation Handling** - Allocate from fragmented space
7. **Multiple Sequential Allocations/Frees** - Stress test with 20+ blocks

**Total Tests:** 35 (5 allocators × 7 tests)

### 2. AVL Complexity Verification (`test_avl_complexity.c`)
Proves O(log n) complexity for Best-Fit and Worst-Fit:

- Tests 8 different scales: 100, 200, 400, 800, 1600, 3200, 6400, 12800 blocks
- Random allocation/free patterns for realistic workload
- Measures actual AVL tree heights
- Verifies time complexity grows logarithmically (not linearly)
- Confirms AVL balance property maintained (height ≤ 1.44 × log₂(n))

**Key Metrics Measured:**
- Tree height vs expected log₂(n)
- Time growth when n doubles
- Number of free blocks in tree
- Balance ratios

### 3. Process-Isolated Test (`test_all_allocators.c`)
Tests each allocator in a separate forked process to ensure:
- No state pollution between allocators
- Arena cleanup works correctly
- Strategy locking prevents mixing

### 4. Buddy Allocator Test (`main.c`)
10 comprehensive tests for buddy allocator:

1. Basic Buddy Allocation (various sizes)
2. Small allocations (< 1 block)
3. Free and reuse
4. Free middle block and reuse
5. Buddy merge (coalescing)
6. Large allocation (65KB)
7. Sequential allocate and free
8. Fragmentation test
9. Edge case - alignment
10. Cleanup verification

## Expected Test Results

### Comprehensive Test Output
```
╔═══════════════════════════════════════════════════════════════╗
║  ✓ ALL ALLOCATORS PASSED COMPREHENSIVE TEST SUITE            ║
║  Results: 5/5 allocators verified                          ║
╚═══════════════════════════════════════════════════════════════╝
```

### AVL Complexity Verification Output

**Best-Fit Allocator:**
```
Blocks (n)   Tree Height   Expected log₂(n)   Ratio
100          5             6.64               0.75
200          6             7.64               0.78
400          7             8.64               0.81
800          7             9.64               0.73
1600         8             10.64              0.75
3200         9             11.64              0.77
6400         9             12.64              0.71
12800        10            13.64              0.73

✓ CONCLUSION: BEST-FIT ALLOCATOR demonstrates O(log n) complexity
  - Tree remains balanced (AVL property maintained)
  - Search time grows logarithmically with input size
```

**Worst-Fit Allocator:**
```
Blocks (n)   Tree Height   Expected log₂(n)   Ratio
100          6             6.64               0.90
200          7             7.64               0.92
400          8             8.64               0.93
800          9             9.64               0.93
1600         11            10.64              1.03
3200         12            11.64              1.03
6400         13            12.64              1.03
12800        14            13.64              1.03

✓ CONCLUSION: WORST-FIT ALLOCATOR demonstrates O(log n) complexity
  - Tree remains balanced (AVL property maintained)
  - Search time grows logarithmically with input size
```

**Time Complexity Verification:**
- When n doubles (×2.0), time increases by only ~1.0-1.3× 
- This is **logarithmic growth**, not linear (which would be ×2.0)
- All growth checks show ✓ O(log n)

## Allocator Performance Summary

| Allocator  | Time Complexity | Data Structure | Tests Passed | Notes |
|-----------|----------------|----------------|--------------|-------|
| First-Fit | O(n)           | Linked List    | 7/7          | Simple, predictable |
| Next-Fit  | O(n)           | Linked List    | 7/7          | Better locality |
| Best-Fit  | O(log n)       | AVL Tree       | 7/7          | Minimizes waste |
| Worst-Fit | O(log n)       | AVL Tree       | 7/7          | Reduces fragmentation |
| Buddy     | O(log n)       | Bins Array     | 10/10        | Fast, power-of-2 only |

## Replication Instructions

### Step 1: Verify Files
Ensure you have these 5 files:
```
2022MT11172mmu.h          (main implementation)
test_comprehensive.c      (all allocators test)
test_avl_complexity.c     (O(log n) proof)
test_all_allocators.c     (process-isolated test)
main.c                    (buddy test)
```

### Step 2: Quick Test (Recommended)
```bash
# Make script executable
chmod +x build_and_test.sh

# Run all tests
./build_and_test.sh
```

**Expected Runtime:** ~5-15 seconds for all tests

### Step 3: Verify Results
Look for these success indicators:
- ✓ All 5 allocators pass 7/7 tests (comprehensive)
- ✓ Both Best-Fit and Worst-Fit show O(log n) conclusion
- ✓ Tree heights grow logarithmically (not linearly)
- ✓ All time growth checks show ✓ O(log n)
- ✓ Buddy allocator passes 10/10 tests

### Step 4: Manual Testing (Optional)
```bash
# Test individual components
gcc -Wall -g -o test_comprehensive test_comprehensive.c -lm
./test_comprehensive

gcc -Wall -g -o test_avl_complexity test_avl_complexity.c -lm
./test_avl_complexity
```

## System Requirements

- **OS:** Linux, macOS, or WSL (Windows Subsystem for Linux)
- **Compiler:** gcc with C99 support
- **Libraries:** Standard C library, math library (-lm)
- **Memory:** At least 100MB free RAM for largest tests

## Troubleshooting

### Compilation Warnings
```
warning: 'ensure_arena' defined but not used
```
**Solution:** Safe to ignore. This is an unused optimization function.

### Runtime Issues
- **mmap failed:** Check system memory limits
- **Segmentation fault:** Ensure sufficient virtual memory available
- **Permission denied:** Run `chmod +x build_and_test.sh`

## Implementation Highlights

### Arena Management
- Uses `mmap()` for memory allocation
- Default arena size: 1MB (configurable via `ARENA_MIN`)
- Automatic expansion when needed
- Proper cleanup with `munmap()`

### AVL Tree (Best/Worst Fit)
- Self-balancing binary search tree
- Guarantees O(log n) height
- Nodes sorted by (size, address) for deterministic behavior
- Rotations maintain balance after insert/delete

### Buddy Allocator
- Independent 4MB memory pool  
- Power-of-2 block sizes
- Automatic splitting and coalescing
- O(log n) allocation and free

### Memory Alignment
- All allocations aligned to 16 bytes (configurable via `ALIGN`)
- Proper handling of alignment in all allocators

## Project Status

✅ **All Features Complete**
- 5 allocators fully implemented and tested
- O(log n) complexity verified with empirical data
- Comprehensive test suite (45+ individual tests)
- Full documentation and replication guide

✅ **All Tests Passing**
- Comprehensive test: 35/35 tests passed
- AVL complexity: Both allocators proven O(log n)
- Process isolation: All strategies verified
- Buddy allocator: 10/10 tests passed


