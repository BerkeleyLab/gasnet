/**
 * Implement hash table data structure using vectors (chaining) to
 * solve collisions
 *
 * example: hashtable_test.c
 *
 * For information about the data structures and algorithms used in the
 * implementation, please see Ch. 12 of Introduction to Algorithms
 * by Thomas H. Cormen, Charles E. Leiserson, Ronald L. Rivest.
 *
 * Yili Zheng
 * Lawrence Berkeley National Laboratory
 * 2009
 */

#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

#include "table.h"

typedef struct gasnete_hashtable
{
  gasnete_table_t ** buckets;
  uint32_t size; /**< hash table size (# of buckets) */
  uint32_t num;  /**< number of elements in the hash table */
} gasnete_hashtable_t;

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * Hash function that determines the bucket for the element with a key
   */
  static inline uint32_t gasnete_hashtable_hash(gasnete_hashtable_t * ht, uint32_t key)
  {
    return (key % ht->size);
  }

  /**
   * Create a hash table
   */
  gasnete_hashtable_t * gasnete_hashtable_create(uint32_t size);

  uint32_t gasnete_hashtable_search(gasnete_hashtable_t * ht, uint32_t key, void ** data);

  uint32_t gasnete_hashtable_insert(gasnete_hashtable_t * ht, uint32_t key, void * data);

  uint32_t gasnete_hashtable_remove(gasnete_hashtable_t * ht, uint32_t key, void ** data);

  void gasnete_hashtable_free(gasnete_hashtable_t * ht);

#ifdef __cplusplus
}
#endif

#endif /* HASH_TABLE_H_ */
