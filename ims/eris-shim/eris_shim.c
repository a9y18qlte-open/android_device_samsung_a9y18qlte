#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef int (*lhash_cmp_func)(const void *a, const void *b);
typedef int (*lhash_cmp_func_helper)(lhash_cmp_func func, const void *a, const void *b);
typedef uint32_t (*lhash_hash_func)(const void *a);
typedef uint32_t (*lhash_hash_func_helper)(lhash_hash_func func, const void *a);

uint32_t OPENSSL_strhash(const char *s);

// BoringSSL made lhash internal in Android 17, so the old chained hash table is
// kept here with the same entry points and semantics.
typedef struct lhash_item {
    void *data;
    struct lhash_item *next;
} lhash_item;

typedef struct lhash_st {
    lhash_item **buckets;
    size_t num_buckets;
    size_t num_items;
    lhash_hash_func hash;
    lhash_cmp_func comp;
} _LHASH;

_LHASH *lh_new(lhash_hash_func hash, lhash_cmp_func comp) {
    _LHASH *lh = calloc(1, sizeof(*lh));
    if (!lh) return NULL;
    lh->num_buckets = 16;
    lh->buckets = calloc(lh->num_buckets, sizeof(*lh->buckets));
    if (!lh->buckets) {
        free(lh);
        return NULL;
    }
    lh->hash = hash;
    lh->comp = comp;
    return lh;
}

void lh_free(_LHASH *lh) {
    if (!lh) return;
    for (size_t i = 0; i < lh->num_buckets; i++) {
        for (lhash_item *it = lh->buckets[i], *next; it; it = next) {
            next = it->next;
            free(it);
        }
    }
    free(lh->buckets);
    free(lh);
}

static lhash_item **lh_find(const _LHASH *lh, const void *data, lhash_hash_func_helper call_hash_func,
                            lhash_cmp_func_helper call_cmp_func, uint32_t *hash) {
    *hash = call_hash_func(lh->hash, data);
    lhash_item **pp = &lh->buckets[*hash % lh->num_buckets];
    while (*pp && call_cmp_func(lh->comp, (*pp)->data, data) != 0) pp = &(*pp)->next;
    return pp;
}

void *lh_retrieve(const _LHASH *lh, const void *data, lhash_hash_func_helper call_hash_func,
                  lhash_cmp_func_helper call_cmp_func) {
    uint32_t hash;
    lhash_item *it = *lh_find(lh, data, call_hash_func, call_cmp_func, &hash);
    return it ? it->data : NULL;
}

int lh_insert(_LHASH *lh, void **old_data, void *data, lhash_hash_func_helper call_hash_func,
              lhash_cmp_func_helper call_cmp_func) {
    uint32_t hash;
    *old_data = NULL;
    lhash_item **pp = lh_find(lh, data, call_hash_func, call_cmp_func, &hash);
    if (*pp) {
        *old_data = (*pp)->data;
        (*pp)->data = data;
        return 1;
    }
    // shortcut: grows by rehashing every item once the load passes 2, never shrinks
    if (lh->num_items > 2 * lh->num_buckets) {
        size_t n = lh->num_buckets * 2;
        lhash_item **nb = calloc(n, sizeof(*nb));
        if (nb) {
            for (size_t i = 0; i < lh->num_buckets; i++) {
                for (lhash_item *it = lh->buckets[i], *next; it; it = next) {
                    next = it->next;
                    size_t b = call_hash_func(lh->hash, it->data) % n;
                    it->next = nb[b];
                    nb[b] = it;
                }
            }
            free(lh->buckets);
            lh->buckets = nb;
            lh->num_buckets = n;
            pp = lh_find(lh, data, call_hash_func, call_cmp_func, &hash);
        }
    }
    lhash_item *it = malloc(sizeof(*it));
    if (!it) return 0;
    it->data = data;
    it->next = NULL;
    *pp = it;
    lh->num_items++;
    return 1;
}

void lh_doall_arg(_LHASH *lh, void (*func)(void *, void *), void *arg) {
    for (size_t i = 0; i < lh->num_buckets; i++) {
        for (lhash_item *it = lh->buckets[i], *next; it; it = next) {
            next = it->next;
            func(it->data, arg);
        }
    }
}

uint32_t lh_strhash(const char *c) {
    return OPENSSL_strhash(c);
}

typedef struct stack_st OPENSSL_STACK;

void *OPENSSL_sk_delete_ptr(OPENSSL_STACK *sk, const void *p);

void *sk_delete_ptr(OPENSSL_STACK *sk, const void *p) {
    return OPENSSL_sk_delete_ptr(sk, p);
}
