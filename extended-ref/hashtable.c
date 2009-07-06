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

HashTable_T * HashTable_create(uint32_t size)
{
  HashTable_T * ht;
  uint32_t i;

  assert(size > 0);
  ht = (HashTable_T *)malloc(sizeof(HashTable_T));
  assert(ht != NULL);
  ht->buckets = (Table_T **)malloc(sizeof(Table_T *)*size);
  assert(ht->buckets != NULL);
  ht->size = size;
  ht->num = 0;

  for (i=0; i<size; i++) {
    ht->buckets[i] = table_create(TABLE_INIT_SIZE);
    assert(ht->buckets[i] != NULL);
  }
 
  return ht;
}

void HashTable_free(HashTable_T * ht)
{
  uint32_t i;

  assert(ht != NULL);
  assert(ht->buckets != NULL);
   
  for (i=0; i<ht->size; i++) {
    assert(ht->buckets[i] != NULL);
    table_free(ht->buckets[i]);
  }

  free(ht->buckets);
  free(ht);
}

uint32_t HashTable_search(HashTable_T * ht, uint32_t key, void ** data)
{
  Table_T * table;
  Tab_Item_T * item;

  assert(ht != NULL);

  table = ht->buckets[HashTable_hash(ht, key)];
  assert (table != NULL);

  item = table_search(table, key);
  if (item == NULL)
    return 1; /* cannot find the item with key */

  if (data != NULL)
    *data = item->data;

  return 0; /* success */
}

uint32_t HashTable_insert(HashTable_T * ht, uint32_t key, void * data)
{
  Table_T * table;
  Tab_Item_T item;
  uint32_t i;

  assert(ht != NULL);

  item.key = key;
  item.data = data;

  i = HashTable_hash(ht, key);
  table = ht->buckets[i];
  assert (table != NULL);

  /* double the size of the table if the table is full */
  if (table->num == table->size) {
    Table_T * new_table;
    new_table = table_create(table->size*2);
    assert(new_table != NULL);
    table_copy(table, new_table);
    ht->buckets[i] = new_table;
    table = new_table;
  }

  ht->num++;
  return table_insert(table, item);
}

uint32_t HashTable_remove(HashTable_T * ht, uint32_t key, void ** data)
{
  Table_T * table;
  Tab_Item_T item;
  uint32_t i, rv;

  assert(ht != NULL);
  i = HashTable_hash(ht, key);
  table = ht->buckets[i];
  assert (table != NULL);
  if (table == NULL)
    return 1;

  rv = table_remove(table, key, &item);
  if (rv == 0 && data != NULL)
    *data = item.data;
  
  ht->num--;
  return rv;
}
