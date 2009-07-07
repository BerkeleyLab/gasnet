/**
 * table.h
 *
 * Yili Zheng
 * LBNL 2009
 */

#ifndef TABLE_H_
#define TABLE_H_

#include <stdint.h>

typedef struct Table_Item
{
	uint32_t key;
	void * data;
} Tab_Item_T;

typedef struct Table
{
	Tab_Item_T *slots;
	uint32_t size;
	uint32_t num;
} Table_T;

#define TABLE_SIZE(tab)	(tab->size) /**< return the maximum size of the queue */
#define TABLE_NUM(tab)	(tab->num)	/**< return the current number of items in the table */

Table_T * table_create(uint32_t size);

Tab_Item_T * table_search(const Table_T * const table, uint32_t key);

uint32_t table_insert(Table_T * const table, Tab_Item_T item);

uint32_t table_remove(Table_T * const table, uint32_t key, Tab_Item_T * deleted);

void table_copy(const Table_T * const src, Table_T * const dst);

void table_free(Table_T * const table);

#endif /* TABLE_H_ */
