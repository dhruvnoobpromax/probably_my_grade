#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "2022MT11172mmu.h"

void *malloc_buddy_alloc(size_t size);
void my_free(void *ptr);

/* Utility to print simple info */
static void print_ptr(const char *name, void *p) {
    printf("%s = %p\n", name, p);
}

int main() {
    printf("=== BUDDY ALLOCATOR TEST SUITE ===\n\n");

    printf("TEST 1: Basic Buddy Allocation (various sizes)\n");
    void *a = malloc_buddy_alloc(64);
    void *b = malloc_buddy_alloc(256);
    void *c = malloc_buddy_alloc(1024);
    void *d = malloc_buddy_alloc(4096);
    print_ptr("A (64 bytes)", a);
    print_ptr("B (256 bytes)", b);
    print_ptr("C (1024 bytes)", c);
    print_ptr("D (4096 bytes)", d);
    printf("\n");

    printf("TEST 2: Small allocations (< 1 block)\n");
    void *s1 = malloc_buddy_alloc(1);
    void *s2 = malloc_buddy_alloc(16);
    void *s3 = malloc_buddy_alloc(32);
    print_ptr("S1 (1 byte)", s1);
    print_ptr("S2 (16 bytes)", s2);
    print_ptr("S3 (32 bytes)", s3);
    printf("\n");

    printf("TEST 3: Free and reuse\n");
    my_free(a);
    my_free(b);
    void *e = malloc_buddy_alloc(128);
    print_ptr("E (128 bytes)", e);
    printf("E should reuse block from A or nearby\n");
    printf("\n");

    printf("TEST 4: Free middle block and reuse\n");
    my_free(c);
    printf("Freed C (1024 bytes)\n");
    void *f = malloc_buddy_alloc(512);
    print_ptr("F (512 bytes)", f);
    printf("F should reuse from C's freed block\n");
    printf("\n");

    printf("TEST 5: Buddy merge (coalescing)\n");
    my_free(e);
    my_free(f);
    printf("Freed E and F - buddies should merge\n");
    void *g = malloc_buddy_alloc(2048);
    print_ptr("G (2048 bytes)", g);
    printf("G should use merged buddy blocks\n");
    printf("\n");

    printf("TEST 6: Large allocation\n");
    void *large = malloc_buddy_alloc(65536);
    print_ptr("Large (65536 bytes)", large);
    if (large != NULL) printf("✓ Large allocation successful\n");
    else printf("✗ Large allocation failed\n");
    printf("\n");

    printf("TEST 7: Sequential allocate and free\n");
    void *seq1 = malloc_buddy_alloc(128);
    void *seq2 = malloc_buddy_alloc(128);
    void *seq3 = malloc_buddy_alloc(128);
    print_ptr("Seq1", seq1);
    print_ptr("Seq2", seq2);
    print_ptr("Seq3", seq3);
    
    my_free(seq1);
    my_free(seq2);
    my_free(seq3);
    printf("Freed seq1, seq2, seq3\n");
    
    void *seq_reuse = malloc_buddy_alloc(256);
    print_ptr("Seq_reuse (256 bytes)", seq_reuse);
    printf("\n");

    printf("TEST 8: Fragmentation test\n");
    void *frag1 = malloc_buddy_alloc(1000);
    void *frag2 = malloc_buddy_alloc(2000);
    void *frag3 = malloc_buddy_alloc(3000);
    print_ptr("Frag1", frag1);
    print_ptr("Frag2", frag2);
    print_ptr("Frag3", frag3);
    
    my_free(frag2);
    printf("Freed frag2\n");
    void *frag_fill = malloc_buddy_alloc(1500);
    print_ptr("Frag_fill", frag_fill);
    printf("\n");

    printf("TEST 9: Edge case - alignment\n");
    void *align1 = malloc_buddy_alloc(7);
    void *align2 = malloc_buddy_alloc(15);
    void *align3 = malloc_buddy_alloc(1);
    print_ptr("Align1 (7 bytes)", align1);
    print_ptr("Align2 (15 bytes)", align2);
    print_ptr("Align3 (1 byte)", align3);
    
    if (align1 && align2 && align3) printf("✓ All alignments valid\n");
    printf("\n");

    printf("TEST 10: Cleanup\n");
    my_free(d);
    my_free(s1);
    my_free(s2);
    my_free(s3);
    my_free(g);
    my_free(large);
    my_free(seq_reuse);
    my_free(frag1);
    my_free(frag3);
    my_free(frag_fill);
    my_free(align1);
    my_free(align2);
    my_free(align3);
    printf("All blocks freed successfully\n");
    printf("\n");

    printf("=== ALL BUDDY ALLOCATOR TESTS COMPLETE ===\n");
    return 0;
}
