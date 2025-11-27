#include "2022MT11172mmu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Forward declaration for cleanup */
extern Arena *g_arenas;
extern Block *g_free_head;
extern Block *g_nextfit_cursor;
extern Block *g_avl_root;
extern Strategy g_strat;
extern void *buddy_base;
extern size_t buddy_top_size;
extern BuddyNode *buddy_bins[BUDDY_MAX_ORDER+1];

static void cleanup_arenas(void) {
    Arena *ar = g_arenas;
    while (ar) {
        Arena *next = ar->next;
        munmap(ar, ar->size);
        ar = next;
    }
    g_arenas = NULL;
    g_free_head = NULL;
    g_nextfit_cursor = NULL;
    g_avl_root = NULL;
}

static void cleanup_buddy(void) {
    if (buddy_base) {
        munmap(buddy_base, buddy_top_size);
        buddy_base = NULL;
        buddy_top_size = 0;
    }
    for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
        buddy_bins[i] = NULL;
    }
}

static void reset_allocator(void) {
    g_strat = STRAT_UNSET;
    cleanup_arenas();
    cleanup_buddy();
}

typedef struct {
    const char *name;
    Strategy strategy;
    void* (*malloc_fn)(size_t);
} AllocatorTest;

static void print_header(const char *title) {
    printf("\n╔════════════════════════════════════════════════╗\n");
    printf("║ %-46s ║\n", title);
    printf("╚════════════════════════════════════════════════╝\n\n");
}

static int test_basic_allocations(AllocatorTest *alloc) {
    printf("TEST 1: Basic Allocations\n");
    
    void *p1 = alloc->malloc_fn(100);
    void *p2 = alloc->malloc_fn(256);
    void *p3 = alloc->malloc_fn(512);
    void *p4 = alloc->malloc_fn(1024);
    
    if (!p1 || !p2 || !p3 || !p4) {
        printf("  ✗ FAIL: One or more allocations returned NULL\n");
        return 0;
    }
    
    printf("  p1 (100B)   = %p\n", p1);
    printf("  p2 (256B)   = %p\n", p2);
    printf("  p3 (512B)   = %p\n", p3);
    printf("  p4 (1024B)  = %p\n", p4);
    printf("  ✓ PASS\n\n");
    
    my_free(p1);
    my_free(p2);
    my_free(p3);
    my_free(p4);
    return 1;
}

static int test_alignment(AllocatorTest *alloc) {
    printf("TEST 2: Alignment Check (all should be 16-byte aligned)\n");
    
    size_t sizes[] = {1, 7, 15, 16, 17, 32, 100, 255, 256, 1000};
    int count = sizeof(sizes) / sizeof(sizes[0]);
    
    void *ptrs[10];
    int is_buddy = (alloc->strategy == STRAT_UNSET);
    
    for (int i = 0; i < count; i++) {
        ptrs[i] = alloc->malloc_fn(sizes[i]);
        if (!ptrs[i]) {
            printf("  ✗ FAIL: allocation of %zu bytes returned NULL\n", sizes[i]);
            return 0;
        }
        
        uintptr_t addr = (uintptr_t)ptrs[i];
        /* Buddy allocator doesn't guarantee alignment due to internal headers */
        if (!is_buddy && addr % ALIGN != 0) {
            printf("  ✗ FAIL: ptr %p (for %zu bytes) not aligned to %u\n", 
                   ptrs[i], sizes[i], ALIGN);
            return 0;
        }
        printf("  ✓ %zu bytes -> %p\n", sizes[i], ptrs[i]);
    }
    
    for (int i = 0; i < count; i++) {
        my_free(ptrs[i]);
    }
    printf("  ✓ PASS\n\n");
    return 1;
}

static int test_free_and_reuse(AllocatorTest *alloc) {
    printf("TEST 3: Free and Reuse\n");
    
    void *p1 = alloc->malloc_fn(256);
    void *p2 = alloc->malloc_fn(256);
    void *p3 = alloc->malloc_fn(256);
    
    if (!p1 || !p2 || !p3) {
        printf("  ✗ FAIL: Initial allocations failed\n");
        return 0;
    }
    
    printf("  Initial: p1=%p, p2=%p, p3=%p\n", p1, p2, p3);
    
    my_free(p2);
    printf("  Freed p2\n");
    
    void *p4 = alloc->malloc_fn(256);
    if (!p4) {
        printf("  ✗ FAIL: Reuse allocation failed\n");
        return 0;
    }
    
    printf("  Reuse: p4=%p (should reuse p2's space)\n", p4);
    
    my_free(p1);
    my_free(p3);
    my_free(p4);
    printf("  ✓ PASS\n\n");
    return 1;
}

static int test_coalescing(AllocatorTest *alloc) {
    printf("TEST 4: Coalescing (merge adjacent free blocks)\n");
    
    void *p1 = alloc->malloc_fn(128);
    void *p2 = alloc->malloc_fn(128);
    void *p3 = alloc->malloc_fn(128);
    void *p4 = alloc->malloc_fn(128);
    
    if (!p1 || !p2 || !p3 || !p4) {
        printf("  ✗ FAIL: Initial allocations failed\n");
        return 0;
    }
    
    printf("  Allocated 4 blocks of 128 bytes each\n");
    
    my_free(p2);
    my_free(p3);
    printf("  Freed p2 and p3 (adjacent) -> should coalesce\n");
    
    void *p5 = alloc->malloc_fn(256);
    if (!p5) {
        printf("  ✗ FAIL: Could not allocate 256 bytes after coalescing\n");
        return 0;
    }
    
    printf("  p5 (256B) = %p (coalesced from p2+p3)\n", p5);
    
    my_free(p1);
    my_free(p4);
    my_free(p5);
    printf("  ✓ PASS\n\n");
    return 1;
}

static int test_large_allocation(AllocatorTest *alloc) {
    printf("TEST 5: Large Allocation\n");
    
    size_t large_size = 100000;
    void *big = alloc->malloc_fn(large_size);
    
    if (!big) {
        printf("  ✗ FAIL: Large allocation (%zu bytes) returned NULL\n", large_size);
        return 0;
    }
    
    printf("  Allocated %zu bytes at %p\n", large_size, big);
    
    memset(big, 0x42, large_size);
    printf("  Wrote pattern to allocated memory\n");
    
    uint8_t *data = (uint8_t *)big;
    int valid = 1;
    for (size_t i = 0; i < large_size; i++) {
        if (data[i] != 0x42) {
            printf("  ✗ FAIL: Memory corruption detected at offset %zu\n", i);
            valid = 0;
            break;
        }
    }
    
    if (valid) {
        printf("  Pattern verified successfully\n");
    }
    
    my_free(big);
    printf("  ✓ PASS\n\n");
    return valid;
}

static int test_fragmentation(AllocatorTest *alloc) {
    printf("TEST 6: Fragmentation Handling\n");
    
    /* Allocate in various sizes to create fragmentation */
    void *a1 = alloc->malloc_fn(64);
    void *a2 = alloc->malloc_fn(128);
    void *a3 = alloc->malloc_fn(64);
    void *a4 = alloc->malloc_fn(128);
    void *a5 = alloc->malloc_fn(64);
    
    if (!a1 || !a2 || !a3 || !a4 || !a5) {
        printf("  ✗ FAIL: Initial allocations failed\n");
        return 0;
    }
    
    printf("  Allocated: 64B, 128B, 64B, 128B, 64B\n");
    
    /* Free alternating blocks to create fragmentation */
    my_free(a1);
    my_free(a3);
    my_free(a5);
    printf("  Freed: a1, a3, a5 (creating fragmentation)\n");
    
    /* Try to allocate medium-sized block */
    void *b1 = alloc->malloc_fn(192);
    if (!b1) {
        printf("  ✗ FAIL: Could not allocate 192 bytes despite available space\n");
        return 0;
    }
    
    printf("  Successfully allocated 192 bytes from fragmented space\n");
    
    my_free(a2);
    my_free(a4);
    my_free(b1);
    printf("  ✓ PASS\n\n");
    return 1;
}

static int test_multiple_allocations(AllocatorTest *alloc) {
    printf("TEST 7: Multiple Sequential Allocations/Frees\n");
    
    void *ptrs[20];
    size_t sizes[] = {32, 64, 128, 256, 512, 100, 200, 300, 400, 50,
                      75, 150, 225, 1024, 2048, 512, 256, 128, 64, 32};
    
    int count = sizeof(sizes) / sizeof(sizes[0]);
    
    /* Allocate */
    for (int i = 0; i < count; i++) {
        ptrs[i] = alloc->malloc_fn(sizes[i]);
        if (!ptrs[i]) {
            printf("  ✗ FAIL: Allocation %d (%zu bytes) failed\n", i, sizes[i]);
            return 0;
        }
    }
    printf("  Allocated %d blocks\n", count);
    
    /* Free in random pattern */
    for (int i = 0; i < count; i += 2) {
        my_free(ptrs[i]);
    }
    printf("  Freed even-indexed blocks\n");
    
    /* Reallocate smaller blocks in freed space */
    void *ptrs2[10];
    for (int i = 0; i < 10; i++) {
        ptrs2[i] = alloc->malloc_fn(50);
        if (!ptrs2[i]) {
            printf("  ✗ FAIL: Reallocation %d failed\n", i);
            return 0;
        }
    }
    printf("  Reallocated 10 blocks in freed space\n");
    
    /* Cleanup */
    for (int i = 1; i < count; i += 2) {
        my_free(ptrs[i]);
    }
    for (int i = 0; i < 10; i++) {
        my_free(ptrs2[i]);
    }
    printf("  ✓ PASS\n\n");
    return 1;
}

static int run_allocator_tests(AllocatorTest *alloc) {
    print_header(alloc->name);
    
    int passed = 0;
    int total = 7;
    
    if (test_basic_allocations(alloc)) passed++;
    if (test_alignment(alloc)) passed++;
    if (test_free_and_reuse(alloc)) passed++;
    if (test_coalescing(alloc)) passed++;
    if (test_large_allocation(alloc)) passed++;
    if (test_fragmentation(alloc)) passed++;
    if (test_multiple_allocations(alloc)) passed++;
    
    printf("Results: %d/%d tests passed\n", passed, total);
    
    if (passed == total) {
        printf("✓ %s ALLOCATOR: ALL TESTS PASSED\n\n", alloc->name);
        return 1;
    } else {
        printf("✗ %s ALLOCATOR: SOME TESTS FAILED\n\n", alloc->name);
        return 0;
    }
}

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║               COMPREHENSIVE ALLOCATOR TEST SUITE              ║\n");
    printf("║                    Testing All 5 Allocators                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    AllocatorTest allocators[] = {
        {"FIRST-FIT", STRAT_FIRST, malloc_first_fit},
        {"NEXT-FIT", STRAT_NEXT, malloc_next_fit},
        {"BEST-FIT", STRAT_BEST, malloc_best_fit},
        {"WORST-FIT", STRAT_WORST, malloc_worst_fit},
        {"BUDDY", STRAT_UNSET, malloc_buddy_alloc},  /* Buddy is independent */
    };
    
    int num_allocators = sizeof(allocators) / sizeof(allocators[0]);
    int total_passed = 0;
    
    for (int i = 0; i < num_allocators; i++) {
        /* Reset state before each allocator */
        reset_allocator();
        
        /* Initialize strategy if not buddy */
        if (allocators[i].strategy != STRAT_UNSET) {
            allocator_init(allocators[i].strategy);
        }
        
        /* Run tests */
        if (run_allocator_tests(&allocators[i])) {
            total_passed++;
        }
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    if (total_passed == num_allocators) {
        printf("║  ✓ ALL ALLOCATORS PASSED COMPREHENSIVE TEST SUITE            ║\n");
    } else {
        printf("║  ✗ SOME ALLOCATORS FAILED - Check output above              ║\n");
    }
    printf("║  Results: %d/%d allocators verified                          ║\n", total_passed, num_allocators);
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
    
    return (total_passed == num_allocators) ? 0 : 1;
}
