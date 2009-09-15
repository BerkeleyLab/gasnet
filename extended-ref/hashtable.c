/**
 * Implement hash table data structure using vectors (chaining) to
 * solve collisions

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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "hashtable.h"

#define TABLE_INIT_SIZE 10

gasnete_hashtable_t * gasnete_hashtable_create(uint32_t size)
{
  gasnete_hashtable_t * ht;
  uint32_t i;

  assert(size > 0);
  ht = (gasnete_hashtable_t *)malloc(sizeof(gasnete_hashtable_t));
  assert(ht != NULL);
  ht->buckets = (gasnete_table_t **)malloc(sizeof(gasnete_table_t *)*size);
  assert(ht->buckets != NULL);
  ht->size = size;
  ht->num = 0;

  for (i=0; i<size; i++) {
    ht->buckets[i] = gasnete_table_create(TABLE_INIT_SIZE);
    assert(ht->buckets[i] != NULL);
  }
 
  return ht;
}

void gasnete_hashtable_free(gasnete_hashtable_t * ht)
{
  uint32_t i;

  assert(ht != NULL);
  assert(ht->buckets != NULL);
   
  for (i=0; i<ht->size; i++) {
    assert(ht->buckets[i] != NULL);
    gasnete_table_free(ht->buckets[i]);
  }

  free(ht->buckets);
  free(ht);
}

uint32_t gasnete_hashtable_search(gasnete_hashtable_t * ht, uint32_t key, void ** data)
{
  gasnete_table_t * table;
  gasnete_table_item_t * item;

  assert(ht != NULL);

  table = ht->buckets[gasnete_hashtable_hash(ht, key)];
  assert (table != NULL);

  item = gasnete_table_search(table, key);
  if (item == NULL)
    return 1; /* cannot find the item with key */

  if (data != NULL)
    *data = item->data;

  return 0; /* success */
}

uint32_t gasnete_hashtable_insert(gasnete_hashtable_t * ht, uint32_t key, void * data)
{
  gasnete_table_t * table;
  gasnete_table_item_t item;
  uint32_t i;

  assert(ht != NULL);

  item.key = key;
  item.data = data;

  i = gasnete_hashtable_hash(ht, key);
  table = ht->buckets[i];
  assert (table != NULL);

  /* double the size of the table if the table is full */
  if (table->num == table->size) {
    gasnete_table_t * new_table;
    new_table = gasnete_table_create(table->size*2);
    assert(new_table != NULL);
    gasnete_table_copy(table, new_table);
    ht->buckets[i] = new_table;
    table = new_table;
  }

  ht->num++;
  return gasnete_table_insert(table, item);
}

uint32_t gasnete_hashtable_remove(gasnete_hashtable_t * ht, uint32_t key, void ** data)
{
  gasnete_table_t * table;
  gasnete_table_item_t item;
  uint32_t i, rv;

  assert(ht != NULL);
  i = gasnete_hashtable_hash(ht, key);
  table = ht->buckets[i];
  assert (table != NULL);
  if (table == NULL)
    return 1;

  rv = gasnete_table_remove(table, key, &item);
  if (rv == 0 && data != NULL)
    *data = item.data;
  
  ht->num--;
  return rv;
}
