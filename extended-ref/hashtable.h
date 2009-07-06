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

typedef struct HashTable
{
  Table_T ** buckets;
  uint32_t size; /**< hash table size (# of buckets) */
  uint32_t num;  /**< number of elements in the hash table */
} HashTable_T;

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * Hash function that determines the bucket for the element with a key
   */
  static inline uint32_t HashTable_hash(HashTable_T * ht, uint32_t key)
  {
    return (key % ht->size);
  }

  /**
   * Create a hash table
   */
  HashTable_T * HashTable_create(uint32_t size);

  uint32_t HashTable_search(HashTable_T * ht, uint32_t key, void ** data);

  uint32_t HashTable_insert(HashTable_T * ht, uint32_t key, void * data);

  uint32_t HashTable_remove(HashTable_T * ht, uint32_t key, void ** data);

  void HashTable_free(HashTable_T * ht);

#ifdef __cplusplus
}
#endif

#endif /* HASH_TABLE_H_ */
