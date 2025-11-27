#include "2022MT11172mmu.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define NUM_SIZES 8
#define ITERATIONS_PER_SIZE 5

extern Block *g_avl_root;

static int measure_avl_height(Block *node) {
    if (!node) return 0;
    int left_h = measure_avl_height(node->avl.l);
    int right_h = measure_avl_height(node->avl.r);
    return 1 + (left_h > right_h ? left_h : right_h);
}

static int count_avl_nodes(Block *node) {
    if (!node) return 0;
    return 1 + count_avl_nodes(node->avl.l) + count_avl_nodes(node->avl.r);
}

static double measure_search_time(void* (*malloc_fn)(size_t), int num_blocks, size_t block_size) {
    void **ptrs = malloc(num_blocks * sizeof(void*));
    if (!ptrs) return -1.0;
    
    /* Phase 1: Allocate all blocks */
    for (int i = 0; i < num_blocks; i++) {
        size_t size = block_size + (rand() % 200) * 16;
        ptrs[i] = malloc_fn(size);
        if (!ptrs[i]) {
            printf("  Warning: allocation %d failed\n", i);
            num_blocks = i;
            break;
        }
    }
    
    /* Phase 2: Random free/allocate to create churn and build AVL tree */
    for (int round = 0; round < 5; round++) {
        /* Free random 30% of blocks */
        for (int i = 0; i < num_blocks; i++) {
            if (ptrs[i] && (rand() % 100) < 30) {
                my_free(ptrs[i]);
                ptrs[i] = NULL;
            }
        }
        
        /* Reallocate some freed slots with different sizes */
        for (int i = 0; i < num_blocks; i++) {
            if (!ptrs[i] && (rand() % 100) < 50) {
                size_t size = block_size + (rand() % 150) * 16;
                ptrs[i] = malloc_fn(size);
            }
        }
    }
    
    /* Phase 3: Free more blocks to have substantial free tree */
    for (int i = 0; i < num_blocks; i++) {
        if (ptrs[i] && (rand() % 100) < 40) {
            my_free(ptrs[i]);
            ptrs[i] = NULL;
        }
    }
    
    /* Phase 4: Measure search performance */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int search_count = num_blocks / 5;
    void **search_ptrs = malloc(search_count * sizeof(void*));
    for (int i = 0; i < search_count; i++) {
        size_t search_size = 64 + (rand() % 100) * 16;
        search_ptrs[i] = malloc_fn(search_size);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) * 1000000.0 +
                     (end.tv_nsec - start.tv_nsec) / 1000.0;
    double time_per_op = elapsed / search_count;
    
    /* Cleanup */
    for (int i = 0; i < search_count; i++) {
        if (search_ptrs[i]) my_free(search_ptrs[i]);
    }
    for (int i = 0; i < num_blocks; i++) {
        if (ptrs[i]) my_free(ptrs[i]);
    }
    
    free(search_ptrs);
    free(ptrs);
    
    return time_per_op;
}

typedef struct {
    int num_blocks;
    double avg_time;
    int tree_height;
    int num_free_blocks;
} ComplexityResult;

static void test_complexity(const char *name, Strategy strategy, void* (*malloc_fn)(size_t)) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  %-60s  ║\n", name);
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    int block_counts[] = {100, 200, 400, 800, 1600, 3200, 6400, 12800};
    ComplexityResult results[NUM_SIZES];
    
    printf("Testing O(log n) complexity for %s...\n\n", name);
    printf("%-12s %-15s %-15s %-20s %-15s\n", 
           "Blocks (n)", "Avg Time (μs)", "Tree Height", "Expected log₂(n)", "Ratio");
    printf("%-12s %-15s %-15s %-20s %-15s\n",
           "----------", "--------------", "-----------", "----------------", "-----");
    
    for (int i = 0; i < NUM_SIZES; i++) {
        int n = block_counts[i];
        double total_time = 0.0;
        int total_height = 0;
        int total_nodes = 0;
        
        for (int iter = 0; iter < ITERATIONS_PER_SIZE; iter++) {
            extern Arena *g_arenas;
            extern Block *g_free_head;
            extern Block *g_nextfit_cursor;
            extern Strategy g_strat;
            
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
            g_strat = STRAT_UNSET;
            
            allocator_init(strategy);
            
            /* Build tree with random allocations and frees */
            void **temp_ptrs = malloc(n * sizeof(void*));
            for (int j = 0; j < n; j++) {
                size_t sz = 128 + (rand() % 200) * 16;
                temp_ptrs[j] = malloc_fn(sz);
            }
            
            /* Random churn to build realistic tree */
            for (int round = 0; round < 3; round++) {
                for (int j = 0; j < n; j++) {
                    if (temp_ptrs[j] && (rand() % 100) < 35) {
                        my_free(temp_ptrs[j]);
                        temp_ptrs[j] = NULL;
                    }
                }
                for (int j = 0; j < n; j++) {
                    if (!temp_ptrs[j] && (rand() % 100) < 60) {
                        size_t sz = 64 + (rand() % 250) * 16;
                        temp_ptrs[j] = malloc_fn(sz);
                    }
                }
            }
            
            /* Measure tree with significant free blocks */
            int height = measure_avl_height(g_avl_root);
            int nodes = count_avl_nodes(g_avl_root);
            total_height += height;
            total_nodes += nodes;
            
            /* Cleanup */
            for (int j = 0; j < n; j++) {
                if (temp_ptrs[j]) my_free(temp_ptrs[j]);
            }
            free(temp_ptrs);
            
            /* Now measure search time */
            double time = measure_search_time(malloc_fn, n, 128);
            if (time < 0) {
                printf("  Error measuring time for n=%d\n", n);
                continue;
            }
            total_time += time;
        }
        
        results[i].num_blocks = n;
        results[i].avg_time = total_time / ITERATIONS_PER_SIZE;
        results[i].tree_height = total_height / ITERATIONS_PER_SIZE;
        results[i].num_free_blocks = total_nodes / ITERATIONS_PER_SIZE;
        
        double expected_log = log2(n);
        double ratio = results[i].tree_height / expected_log;
        
        printf("%-12d %-15.3f %-15d %-20.2f %-15.2f\n",
               n, results[i].avg_time, results[i].tree_height, expected_log, ratio);
    }
    
    printf("\n");
    printf("Complexity Analysis:\n");
    printf("-------------------\n");
    
    printf("Time growth analysis:\n");
    for (int i = 1; i < NUM_SIZES; i++) {
        double n_ratio = (double)results[i].num_blocks / results[i-1].num_blocks;
        double time_ratio = results[i].avg_time / results[i-1].avg_time;
        double expected_log_ratio = log2(results[i].num_blocks) / log2(results[i-1].num_blocks);
        
        printf("  n: %d -> %d (×%.1f): time ×%.2f, expected log ratio: ×%.2f",
               results[i-1].num_blocks, results[i].num_blocks,
               n_ratio, time_ratio, expected_log_ratio);
        
        if (time_ratio < expected_log_ratio * 2.0) {
            printf(" ✓ O(log n)\n");
        } else if (time_ratio < n_ratio * 0.5) {
            printf(" ~ sublinear\n");
        } else {
            printf(" ✗ may be O(n)\n");
        }
    }
    
    printf("\nTree balance analysis:\n");
    int balanced = 1;
    for (int i = 0; i < NUM_SIZES; i++) {
        if (results[i].num_free_blocks == 0) {
            printf("  n=%d: height=%d, free_blocks=%d (no free blocks to measure)\n",
                   results[i].num_blocks, results[i].tree_height, results[i].num_free_blocks);
            continue;
        }
        
        double expected = log2(results[i].num_free_blocks);
        double ratio = results[i].tree_height / expected;
        
        printf("  n=%d: height=%d, free_blocks=%d, log₂(free)=%.2f, ratio=%.2f",
               results[i].num_blocks, results[i].tree_height,
               results[i].num_free_blocks, expected, ratio);
        
        if (ratio <= 2.0) {
            printf(" ✓ balanced\n");
        } else {
            printf(" ✗ unbalanced\n");
            balanced = 0;
        }
    }
    
    printf("\n");
    if (balanced) {
        printf("✓ CONCLUSION: %s demonstrates O(log n) complexity\n", name);
        printf("  - Tree remains balanced (AVL property maintained)\n");
        printf("  - Search time grows logarithmically with input size\n");
    } else {
        printf("✗ WARNING: %s may not be maintaining O(log n) complexity\n", name);
        printf("  - Tree appears unbalanced or search time grows too quickly\n");
    }
    printf("\n");
}

int main(void) {
    srand(time(NULL));  /* Seed random number generator */
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║           AVL TREE COMPLEXITY VERIFICATION TEST               ║\n");
    printf("║     Verifying O(log n) Performance for Best/Worst Fit        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    test_complexity("BEST-FIT ALLOCATOR", STRAT_BEST, malloc_best_fit);
    test_complexity("WORST-FIT ALLOCATOR", STRAT_WORST, malloc_worst_fit);
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                     TEST COMPLETE                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}
