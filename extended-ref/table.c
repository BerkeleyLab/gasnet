/*
 * table.c - implements a table data structure
 *
 * Yili Zheng
 * Lawrence Berkeley National Laboratory
 * 2009
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "table.h"

Table_T * table_create(uint32_t size)
{
  Table_T * table;

  assert(size > 0);
  table = (Table_T *)malloc(sizeof(Table_T));
  assert(table != NULL);

  table->slots = (Tab_Item_T *)malloc(sizeof(Tab_Item_T)*size);
  assert(table->slots != NULL);

  table->size = size;
  table->num = 0;

  return table;
}

Tab_Item_T * table_search(const Table_T * const table, uint32_t key)
{
  uint32_t i;
  Tab_Item_T * slots;

  assert(table != NULL);
  slots = table->slots;
  for (i=0; i<table->num; i++)
    if (key == slots[i].key)
      return &slots[i];

  return NULL; /* item with key is not found in the table */
}

uint32_t table_insert(Table_T * const table, Tab_Item_T item)
{
  if (table->num >= table->size)
    return 1; /* insertion failed because the table is full */

  /* added the item to the end of the table */
  table->slots[table->num] = item;
  table->num++;

  return 0; /* success */
}

uint32_t table_remove(Table_T * const table, uint32_t key, Tab_Item_T * deleted)
{
  Tab_Item_T item;
  uint32_t i;
  Tab_Item_T * slots;

  assert(table != NULL);
  slots = table->slots;
  for (i=0; i<table->num; i++)
    if (key == slots[i].key) {
        item = table->slots[i];
        if (deleted != NULL) {
          deleted->key = item.key;
          deleted->data = item.data;
        }

        if (i < table->num-1)
          table->slots[i] = table->slots[table->num-1];

        table->num--;
        return 0;
      }

#ifdef DEBUG
  printf("Trying to remove an item not in the table!\n");
#endif

  return 1; /* item not in the table */
}

void table_copy(const Table_T * const src, Table_T * const dst)
{
  uint32_t i;
  Tab_Item_T * src_slots, * dst_slots;

  assert(dst->size >= src->num);

  src_slots = src->slots;
  dst_slots = dst->slots;
  for (i=0; i<src->num; i++)
    dst_slots[i] = src_slots[i];

  dst->num = src->num;
}

void table_free(Table_T * const table)
{
  assert(table != NULL);

  free(table->slots);
  free(table);
}
