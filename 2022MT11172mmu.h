#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#ifndef ARENA_MIN
#define ARENA_MIN (1u<<20)
#endif
#ifndef ALIGN
#define ALIGN 16u
#endif
#ifndef BUDDY_MAX_ORDER
#define BUDDY_MAX_ORDER 26
#endif

#define ALIGN_UP(x,a) (((x)+((a)-1)) & ~((a)-1))

typedef struct Block Block;

typedef struct {
    Block *l, *r;
    int8_t h;
} AVL;

typedef struct Block {
    size_t size;
    uint32_t is_free;
    uint32_t _pad;
    Block *prev_phys;
    Block *next_phys;
    Block *next_free;
    AVL avl;
} Block;

typedef struct Arena {
    struct Arena *next;
    size_t size;
} Arena;

static Arena *g_arenas = NULL;
#define HDR_SZ ALIGN_UP(sizeof(Block), ALIGN)

static inline void *blk_to_ptr(Block *b){ return (void*)((uint8_t*)b + HDR_SZ); }
static inline Block *ptr_to_blk(void *p){ return (Block*)((uint8_t*)p - HDR_SZ); }

typedef enum {
    STRAT_UNSET = -1,
    STRAT_FIRST = 0,
    STRAT_NEXT  = 1,
    STRAT_BEST  = 2,
    STRAT_WORST = 3
} Strategy;

static Strategy g_strat = STRAT_UNSET;
static void index_insert(Block *b);
static void index_remove(Block *b);
static Block* index_find(size_t need);

static Block *g_free_head = NULL;
static Block *g_nextfit_cursor = NULL;
static Block *g_avl_root = NULL;


static Arena* map_arena(size_t min_usable){
    size_t need = sizeof(Arena) + HDR_SZ + min_usable;
    if (need < ARENA_MIN) need = ARENA_MIN;

    void *mem = mmap(NULL, need, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return NULL;

    Arena *ar = (Arena*)mem;
    ar->next = g_arenas;
    ar->size = need;
    g_arenas = ar;

    Block *b = (Block*)((uint8_t*)ar + sizeof(Arena));
    b->size = need - sizeof(Arena) - HDR_SZ;
    b->is_free = 1;
    b->_pad = 0;
    b->prev_phys = NULL;
    b->next_phys = NULL;
    b->next_free = NULL;
    b->avl.l = b->avl.r = NULL;
    b->avl.h = 1;

    index_insert(b);
    return ar;
}

__attribute__((constructor))
static void init_once(void){ (void)map_arena(ARENA_MIN); }

static void fl_push_sorted(Block *b){
    Block **pp = &g_free_head;
    while (*pp && *pp < b) pp = &(*pp)->next_free;
    b->next_free = *pp;
    *pp = b;
}

static void fl_remove(Block *b){
    Block **pp = &g_free_head;
    while (*pp && *pp != b) pp = &(*pp)->next_free;
    if (*pp) *pp = b->next_free;
    if (g_nextfit_cursor == b)
        g_nextfit_cursor = b->next_free ? b->next_free : g_free_head;
    b->next_free = NULL;
}

static Block* fl_first_fit(size_t need){
    Block *cur = g_free_head;
    while (cur){
        if (cur->size >= need) return cur;
        cur = cur->next_free;
    }
    return NULL;
}

static Block* fl_next_fit(size_t need){
    if (!g_nextfit_cursor) g_nextfit_cursor = g_free_head;
    if (!g_nextfit_cursor) return NULL;

    Block *start = g_nextfit_cursor, *cur = g_nextfit_cursor;
    do {
        if (cur->size >= need){
            g_nextfit_cursor = cur->next_free ? cur->next_free : g_free_head;
            return cur;
        }
        cur = cur->next_free ? cur->next_free : g_free_head;
    } while(cur && cur != start);
    return NULL;
}

static inline size_t key_size(Block *b){ return b->size; }
static inline uintptr_t key_addr(Block *b){ return (uintptr_t)(void*)b; }

static int cmp_block(Block *a, Block *b){
    if (key_size(a) < key_size(b)) return -1;
    if (key_size(a) > key_size(b)) return 1;
    if (key_addr(a) < key_addr(b)) return -1;
    if (key_addr(a) > key_addr(b)) return 1;
    return 0;
}

static int8_t height(Block *n){ return n ? n->avl.h : 0; }

static void upd(Block *n){
    int8_t hl = height(n->avl.l), hr = height(n->avl.r);
    n->avl.h = (hl > hr ? hl : hr) + 1;
}

static Block* rotR(Block *y){
    Block *x = y->avl.l, *T2 = x->avl.r;
    x->avl.r = y;
    y->avl.l = T2;
    upd(y);
    upd(x);
    return x;
}

static Block* rotL(Block *x){
    Block *y = x->avl.r, *T2 = y->avl.l;
    y->avl.l = x;
    x->avl.r = T2;
    upd(x);
    upd(y);
    return y;
}

static int balance_factor(Block *n){ return n ? height(n->avl.l) - height(n->avl.r) : 0; }

static Block* avl_insert_rec(Block *root, Block *node){
    if (!root) return node;
    int c = cmp_block(node, root);
    if (c < 0) root->avl.l = avl_insert_rec(root->avl.l, node);
    else if (c > 0) root->avl.r = avl_insert_rec(root->avl.r, node);
    else return root;
    upd(root);
    int bf = balance_factor(root);
    if (bf > 1 && cmp_block(node, root->avl.l) < 0) return rotR(root);
    if (bf < -1 && cmp_block(node, root->avl.r) > 0) return rotL(root);
    if (bf > 1 && cmp_block(node, root->avl.l) > 0){
        root->avl.l = rotL(root->avl.l);
        return rotR(root);
    }
    if (bf < -1 && cmp_block(node, root->avl.r) < 0){
        root->avl.r = rotR(root->avl.r);
        return rotL(root);
    }
    return root;
}

static Block* avl_lower_bound(Block *root, size_t need){
    Block *ans = NULL;
    while (root){
        if (need <= root->size){ ans = root; root = root->avl.l; }
        else root = root->avl.r;
    }
    return ans;
}

// Find the rightmost (largest) block >= need in O(log n) time
static Block* avl_rightmost_ge(Block *root, size_t need){
    Block *ans = NULL;
    while (root){
        if (root->size >= need){
            // This block fits; remember it as a candidate
            ans = root;
            // Keep searching right for larger blocks
            root = root->avl.r;
        }else{
            // Block too small; go right (left would be even smaller)
            root = root->avl.r;
        }
    }
    return ans;
}

static Block* avl_min(Block *n){
    while(n && n->avl.l) n = n->avl.l;
    return n;
}

static Block* avl_delete_rec(Block *root, Block *node){
    if (!root) return NULL;
    int c = cmp_block(node, root);
    if (c < 0){
        root->avl.l = avl_delete_rec(root->avl.l, node);
    } else if (c > 0){
        root->avl.r = avl_delete_rec(root->avl.r, node);
    } else {
        if (!root->avl.l) return root->avl.r;
        if (!root->avl.r) return root->avl.l;
        Block *s = avl_min(root->avl.r);
        root->avl.r = avl_delete_rec(root->avl.r, s);
        s->avl.l = root->avl.l;
        s->avl.r = root->avl.r;
        root = s;
    }
    upd(root);
    int bf = balance_factor(root);
    if (bf > 1 && balance_factor(root->avl.l) >= 0) return rotR(root);
    if (bf > 1 && balance_factor(root->avl.l) < 0){
        root->avl.l = rotL(root->avl.l);
        return rotR(root);
    }
    if (bf < -1 && balance_factor(root->avl.r) <= 0) return rotL(root);
    if (bf < -1 && balance_factor(root->avl.r) > 0){
        root->avl.r = rotR(root->avl.r);
        return rotL(root);
    }
    return root;
}

static void avl_insert(Block *b){
    b->avl.l = b->avl.r = NULL;
    b->avl.h = 1;
    g_avl_root = avl_insert_rec(g_avl_root, b);
}

static void avl_erase(Block *b){
    g_avl_root = avl_delete_rec(g_avl_root, b);
}

// ======================= Index dispatch (strict independence) =======================

static void ensure_arena(size_t need){
    if (g_strat == STRAT_UNSET) return;
    Block *probe = index_find(need);
    if (!probe) (void)map_arena(need);
}

static void index_insert(Block *b){
    if (g_strat == STRAT_FIRST || g_strat == STRAT_NEXT || g_strat == STRAT_UNSET){
        fl_push_sorted(b);
    } else {
        avl_insert(b);
    }
}

static void index_remove(Block *b){
    if (g_strat == STRAT_FIRST || g_strat == STRAT_NEXT){
        fl_remove(b);
    } else if (g_strat == STRAT_BEST || g_strat == STRAT_WORST){
        avl_erase(b);
    } else {
        fl_remove(b);
    }
}

static Block* index_find(size_t need){
    if (g_strat == STRAT_FIRST)  return fl_first_fit(need);
    if (g_strat == STRAT_NEXT)   return fl_next_fit(need);
    if (g_strat == STRAT_BEST)   return avl_lower_bound(g_avl_root, need);
    if (g_strat == STRAT_WORST)  return avl_rightmost_ge(g_avl_root, need);
    return fl_first_fit(need);
}

// ======================= Split & Coalesce (index-agnostic) =======================

static Block* split_block(Block *b, size_t need){
    size_t left = b->size - need;
    size_t min_split = HDR_SZ + ALIGN_UP(1, ALIGN);
    if (left < min_split) return b;

    uint8_t *base = (uint8_t*)b;
    Block *alloc = b;
    Block *rem = (Block*)(base + HDR_SZ + need);

    rem->size = left - HDR_SZ;
    rem->is_free = 1; rem->_pad = 0;
    rem->prev_phys = alloc;
    rem->next_phys = alloc->next_phys;
    if (rem->next_phys) rem->next_phys->prev_phys = rem;

    rem->next_free = NULL;
    rem->avl.l = rem->avl.r = NULL; rem->avl.h = 1;

    alloc->size = need;
    alloc->next_phys = rem;

    index_insert(rem);
    return alloc;
}

static void coalesce_and_insert(Block *b){
    Block *L = b->prev_phys, *R = b->next_phys;

    if (L && L->is_free){
        index_remove(L);
        L->size += HDR_SZ + b->size;
        L->next_phys = b->next_phys;
        if (b->next_phys) b->next_phys->prev_phys = L;
        b = L;
    }

    if (R && R->is_free){
        index_remove(R);
        b->size += HDR_SZ + R->size;
        b->next_phys = R->next_phys;
        if (R->next_phys) R->next_phys->prev_phys = b;
    }

    b->next_free = NULL;
    b->avl.l = b->avl.r = NULL;
    b->avl.h = 1;

    index_insert(b);
}

// ======================= Allocation core (general heap) =======================

static Block* allocate_general(size_t size){
    if (size == 0) return NULL;
    size = ALIGN_UP(size, ALIGN);

    Block *b = index_find(size);
    if (!b){
        if (!map_arena(size)) return NULL;
        b = index_find(size);
        if (!b) return NULL;
    }

    index_remove(b);
    b = split_block(b, size);
    b->is_free = 0;
    return b;
}

// ======================= Public malloc flavors (lock-in strategy) =======================

static int lock_strategy(Strategy s){
    if (g_strat == STRAT_UNSET){ g_strat = s; return 1; }
    if (g_strat != s){
        fprintf(stderr, "[allocator] ERROR: mixed strategies in one run (%d vs %d)\n",(int)g_strat,(int)s);
        abort();
    }
    return 1;
}

void allocator_init(Strategy s){ lock_strategy(s); }

void* malloc_first_fit(size_t size){ lock_strategy(STRAT_FIRST); Block *b=allocate_general(size); return b? blk_to_ptr(b):NULL; }
void* malloc_next_fit (size_t size){ lock_strategy(STRAT_NEXT ); Block *b=allocate_general(size); return b? blk_to_ptr(b):NULL; }
void* malloc_best_fit (size_t size){ lock_strategy(STRAT_BEST ); Block *b=allocate_general(size); return b? blk_to_ptr(b):NULL; }
void* malloc_worst_fit(size_t size){ lock_strategy(STRAT_WORST); Block *b=allocate_general(size); return b? blk_to_ptr(b):NULL; }

static void free_general(void *ptr){
    if (!ptr) return;
    Block *b = ptr_to_blk(ptr);
    if (b->is_free) return;
    b->is_free = 1;
    coalesce_and_insert(b);
}

// ======================= Buddy allocator (independent) =======================

typedef struct BuddyNode { struct BuddyNode *next; } BuddyNode;
static void *buddy_base=NULL; static size_t buddy_top_size=0;
static size_t buddy_order0=0; static size_t buddy_pool_order=0;
static BuddyNode *buddy_bins[BUDDY_MAX_ORDER+1];

static inline size_t order_size(size_t o){ return (size_t)1<<o; }
static inline size_t ptr_off(void *p){ return (size_t)((uint8_t*)p - (uint8_t*)buddy_base); }
static inline void* off_ptr(size_t off){ return (void*)((uint8_t*)buddy_base + off); }

static void buddy_push(size_t o, void *p){ BuddyNode *n=(BuddyNode*)p; n->next=buddy_bins[o]; buddy_bins[o]=n; }
static void* buddy_pop(size_t o){ BuddyNode *n=buddy_bins[o]; if (!n) return NULL; buddy_bins[o]=n->next; return (void*)n; }

static void buddy_init_pool(size_t min_bytes){
    size_t min_block = ALIGN_UP(sizeof(BuddyNode)+sizeof(size_t), ALIGN);
    buddy_order0 = 0; while (order_size(buddy_order0) < min_block) buddy_order0++;
    
    buddy_pool_order = 22;
    if (min_bytes > (size_t)(1<<22)){
        size_t need = ALIGN_UP(min_bytes + sizeof(size_t), order_size(buddy_order0));
        buddy_pool_order = buddy_order0;
        while (order_size(buddy_pool_order) < need && buddy_pool_order < BUDDY_MAX_ORDER) buddy_pool_order++;
    }
    
    size_t total = order_size(buddy_pool_order);
    void *mem = mmap(NULL,total,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (mem==MAP_FAILED){ buddy_base=NULL; buddy_top_size=0; return; }
    buddy_base=mem; buddy_top_size=total;
    for (size_t i=0;i<=BUDDY_MAX_ORDER;i++) buddy_bins[i]=NULL;
    buddy_push(buddy_pool_order,buddy_base);
}

static void buddy_try_merge(size_t order, void *p){
    if (!p || !buddy_base) return;
    size_t off=ptr_off(p);
    if (off>=buddy_top_size) return;

    for (; order<buddy_pool_order; ++order){
        size_t block_sz=order_size(order);
        size_t buddy_off = off ^ block_sz;
        if (buddy_off>=buddy_top_size) break;
        void *buddy_ptr=off_ptr(buddy_off);

        BuddyNode **pp=&buddy_bins[order], *prev=NULL, *cur=*pp;
        int found=0;
        while(cur){
            if ((void*)cur==buddy_ptr){
                if(prev) prev->next=cur->next; else *pp=cur->next;
                found=1; break;
            }
            prev=cur; cur=cur->next;
        }
        if (found){
            off = (buddy_off<off)?buddy_off:off;
            continue;
        } else {
            buddy_push(order, off_ptr(off));
            return;
        }
    }
    buddy_push(order, off_ptr(off));
}

void* malloc_buddy_alloc(size_t size){
    if (size==0) return NULL;
    size=ALIGN_UP(size,ALIGN);
    if (!buddy_base) buddy_init_pool(size);
    if (!buddy_base) return NULL;

    size_t need = size + sizeof(size_t);
    size_t order=buddy_order0;
    while (order_size(order) < need && order<=buddy_pool_order) order++;
    if (order>buddy_pool_order) return NULL;

    size_t k=order;
    while (k<=buddy_pool_order && !buddy_bins[k]) k++;
    if (k>buddy_pool_order) return NULL;

    void *p=buddy_pop(k);
    while (k>order){
        k--;
        size_t half=order_size(k);
        void *right=(void*)((uint8_t*)p+half);
        buddy_push(k,right);
    }

    size_t *hdr=(size_t*)p;
    *hdr = (size_t)0x8000000000000000ull | order;
    void *user=(void*)(hdr+1);
    return user;
}

static int is_buddy_ptr(void *ptr, size_t *out_order, void **out_raw){
    if (!buddy_base) return 0;
    uintptr_t a=(uintptr_t)ptr;
    uintptr_t L=(uintptr_t)buddy_base;
    uintptr_t R=L+buddy_top_size;
    if (a<=L || a>=R) return 0;
    size_t *hdr=(size_t*)ptr - 1;
    size_t tag=*hdr;
    if ((tag & 0x8000000000000000ull)==0) return 0;
    size_t order = tag & 0x7fffffffffffffffull;
    if (order>buddy_pool_order) return 0;
    if (out_order) *out_order=order;
    if (out_raw) *out_raw=(void*)hdr;
    return 1;
}

// ======================= Unified free =======================

void my_free(void *ptr){
    if (!ptr) return;
    size_t ord; void *raw;
    if (is_buddy_ptr(ptr, &ord, &raw)){ buddy_try_merge(ord, raw); return; }
    free_general(ptr);
}

#ifdef TEST_ALLOCATOR
static void dump_free_list(void){
    fprintf(stderr,"[free_list]");
    Block *c=g_free_head; while(c){ fprintf(stderr," ->(%zu)",c->size); c=c->next_free; }
    fprintf(stderr,"\n");
}
static int avl_height(Block *n){ return n? 1 + (avl_height(n->avl.l)>avl_height(n->avl.r)?avl_height(n->avl.l):avl_height(n->avl.r)) : 0; }

int main(void){
    allocator_init(STRAT_FIRST);

    void *a = malloc_first_fit(1000);
    void *b = malloc_first_fit(2000);
    void *c = malloc_first_fit(3000);
    fprintf(stderr,"alloc a=%p b=%p c=%p\n",a,b,c);

    my_free(b);
    my_free(a);
    my_free(c);

    void *e = malloc_buddy_alloc(1024);
    void *f = malloc_buddy_alloc(4096);
    fprintf(stderr,"buddy e=%p f=%p\n", e,f);
    my_free(e); my_free(f);

    for (size_t s=1;s<=64;++s){
        void *p = malloc_first_fit(s);
        assert(((uintptr_t)p % ALIGN)==0);
        my_free(p);
    }

    if (g_strat==STRAT_FIRST || g_strat==STRAT_NEXT) dump_free_list();
    if (g_strat==STRAT_BEST || g_strat==STRAT_WORST) fprintf(stderr,"[avl height] %d\n", avl_height(g_avl_root));
    return 0;
}
#endif
