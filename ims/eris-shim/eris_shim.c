#include <stddef.h>
#include <stdint.h>

typedef struct lhash_st _LHASH;
typedef int (*lhash_cmp_func)(const void *a, const void *b);
typedef int (*lhash_cmp_func_helper)(lhash_cmp_func func, const void *a, const void *b);
typedef uint32_t (*lhash_hash_func)(const void *a);
typedef uint32_t (*lhash_hash_func_helper)(lhash_hash_func func, const void *a);

_LHASH *OPENSSL_lh_new(lhash_hash_func hash, lhash_cmp_func comp);
void OPENSSL_lh_free(_LHASH *lh);
void *OPENSSL_lh_retrieve(const _LHASH *lh, const void *data,
                          lhash_hash_func_helper call_hash_func,
                          lhash_cmp_func_helper call_cmp_func);
int OPENSSL_lh_insert(_LHASH *lh, void **old_data, void *data,
                      lhash_hash_func_helper call_hash_func,
                      lhash_cmp_func_helper call_cmp_func);
void OPENSSL_lh_doall_arg(_LHASH *lh, void (*func)(void *, void *), void *arg);
uint32_t OPENSSL_strhash(const char *s);

_LHASH *lh_new(lhash_hash_func hash, lhash_cmp_func comp) {
    return OPENSSL_lh_new(hash, comp);
}

void lh_free(_LHASH *lh) {
    OPENSSL_lh_free(lh);
}

void *lh_retrieve(const _LHASH *lh, const void *data, lhash_hash_func_helper call_hash_func,
                  lhash_cmp_func_helper call_cmp_func) {
    return OPENSSL_lh_retrieve(lh, data, call_hash_func, call_cmp_func);
}

int lh_insert(_LHASH *lh, void **old_data, void *data, lhash_hash_func_helper call_hash_func,
              lhash_cmp_func_helper call_cmp_func) {
    return OPENSSL_lh_insert(lh, old_data, data, call_hash_func, call_cmp_func);
}

void lh_doall_arg(_LHASH *lh, void (*func)(void *, void *), void *arg) {
    OPENSSL_lh_doall_arg(lh, func, arg);
}

uint32_t lh_strhash(const char *c) {
    return OPENSSL_strhash(c);
}
