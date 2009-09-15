/*
 * table.c - implements a table data structure
 *
 * Lawrence Berkeley National Laboratory
 * 2009
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "table.h"

gasnete_table_t * gasnete_table_create(uint32_t size)
{
  gasnete_table_t * table;

  assert(size > 0);
  table = (gasnete_table_t *)malloc(sizeof(gasnete_table_t));
  assert(table != NULL);

  table->slots = (gasnete_table_item_t *)malloc(sizeof(gasnete_table_item_t)*size);
  assert(table->slots != NULL);

  table->size = size;
  table->num = 0;

  return table;
}

gasnete_table_item_t * gasnete_table_search(const gasnete_table_t * const table, uint32_t key)
{
  uint32_t i;
  gasnete_table_item_t * slots;

  assert(table != NULL);
  slots = table->slots;
  for (i=0; i<table->num; i++)
    if (key == slots[i].key)
      return &slots[i];

  return NULL; /* item with key is not found in the table */
}

uint32_t gasnete_table_insert(gasnete_table_t * const table, gasnete_table_item_t item)
{
  if (table->num >= table->size)
    return 1; /* insertion failed because the table is full */

  /* added the item to the end of the table */
  table->slots[table->num] = item;
  table->num++;

  return 0; /* success */
}

uint32_t gasnete_table_remove(gasnete_table_t * const table, uint32_t key, gasnete_table_item_t * deleted)
{
  gasnete_table_item_t item;
  uint32_t i;
  gasnete_table_item_t * slots;

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

void gasnete_table_copy(const gasnete_table_t * const src, gasnete_table_t * const dst)
{
  uint32_t i;
  gasnete_table_item_t * src_slots, * dst_slots;

  assert(dst->size >= src->num);

  src_slots = src->slots;
  dst_slots = dst->slots;
  for (i=0; i<src->num; i++)
    dst_slots[i] = src_slots[i];

  dst->num = src->num;
}

void gasnete_table_free(gasnete_table_t * const table)
{
  assert(table != NULL);

  free(table->slots);
  free(table);
}
