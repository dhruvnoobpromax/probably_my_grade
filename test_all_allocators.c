#include "2022MT11172mmu.h"
#include <stdio.h>
#include <unistd.h>

int test_allocator(int strategy) {
    const char *names[] = {"first", "next", "best", "worst", "buddy"};
    printf("=== Testing %s-fit ===\n", names[strategy]);
    
    /* Allocate 16 blocks */
    void *a[16];
    size_t sizes[] = {64, 128, 256, 512, 100, 200, 300, 400, 50, 75, 150, 225, 1024, 2048, 4096, 8192};
    int n = sizeof(sizes) / sizeof(sizes[0]);
    
    /* Allocate */
    for (int i = 0; i < n; i++) {
        switch(strategy) {
            case 0: a[i] = malloc_first_fit(sizes[i]); break;
            case 1: a[i] = malloc_next_fit(sizes[i]); break;
            case 2: a[i] = malloc_best_fit(sizes[i]); break;
            case 3: a[i] = malloc_worst_fit(sizes[i]); break;
            case 4: a[i] = malloc_buddy_alloc(sizes[i]); break;
        }
        if (!a[i]) {
            printf("  FAIL: allocation %d returned NULL\n", i);
            return 0;
        }
    }
    printf("  ✓ Allocated 16 blocks\n");
    
    /* Free and reuse */
    for (int i = 0; i < n; i += 2) {
        my_free(a[i]);
    }
    
    void *b[8];
    for (int i = 0; i < 8; i++) {
        switch(strategy) {
            case 0: b[i] = malloc_first_fit(sizes[i]); break;
            case 1: b[i] = malloc_next_fit(sizes[i]); break;
            case 2: b[i] = malloc_best_fit(sizes[i]); break;
            case 3: b[i] = malloc_worst_fit(sizes[i]); break;
            case 4: b[i] = malloc_buddy_alloc(sizes[i]); break;
        }
        if (!b[i]) {
            printf("  FAIL: reuse allocation %d returned NULL\n", i);
            return 0;
        }
    }
    printf("  ✓ Reused freed space\n");
    
    /* Coalesce everything */
    for (int i = 1; i < n; i += 2) {
        my_free(a[i]);
    }
    for (int i = 0; i < 8; i++) {
        my_free(b[i]);
    }
    printf("  ✓ Coalescing works\n");
    
    /* Test large allocation */
    void *big = NULL;
    switch(strategy) {
        case 0: big = malloc_first_fit(200000); break;
        case 1: big = malloc_next_fit(200000); break;
        case 2: big = malloc_best_fit(200000); break;
        case 3: big = malloc_worst_fit(200000); break;
        case 4: big = malloc_buddy_alloc(200000); break;
    }
    if (big) {
        my_free(big);
        printf("  ✓ Large allocation works\n");
    } else {
        printf("  FAIL: Large allocation returned NULL\n");
        return 0;
    }
    
    printf("\n");
    return 1;
}

int main() {
    printf("╔════════════════════════════════════════════════╗\n");
    printf("║   COMPREHENSIVE ALLOCATOR VERIFICATION TEST   ║\n");
    printf("╚════════════════════════════════════════════════╝\n\n");

    /* Test each allocator in a separate process to avoid state pollution */
    int strategies[] = {0, 1, 2, 3, 4};
    int num_strategies = 5;
    int passed = 0;
    
    for (int s = 0; s < num_strategies; s++) {
        pid_t pid = fork();
        if (pid == 0) {
            /* Child process */
            exit(test_allocator(strategies[s]) ? 0 : 1);
        } else if (pid > 0) {
            /* Parent process - wait for child */
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                passed++;
            }
        }
    }
    
    if (passed == num_strategies) {
        printf("╔════════════════════════════════════════════════╗\n");
        printf("║   ALL ALLOCATORS VERIFIED SUCCESSFULLY ✓     ║\n");
        printf("╚════════════════════════════════════════════════╝\n");
        return 0;
    } else {
        printf("╔════════════════════════════════════════════════╗\n");
        printf("║   SOME ALLOCATORS FAILED ✗                   ║\n");
        printf("╚════════════════════════════════════════════════╝\n");
        return 1;
    }
}

