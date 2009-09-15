/**
 * table.h
 *
 * Yili Zheng
 * LBNL 2009
 */

#ifndef TABLE_H_
#define TABLE_H_

#include <stdint.h>

typedef struct gasnete_table_item
{
	uint32_t key;
	void * data;
} gasnete_table_item_t;

typedef struct gasnete_table
{
	gasnete_table_item_t *slots;
	uint32_t size;
	uint32_t num;
} gasnete_table_t;

#define TABLE_SIZE(tab)	(tab->size) /**< return the maximum size of the queue */
#define TABLE_NUM(tab)	(tab->num)	/**< return the current number of items in the table */

gasnete_table_t * gasnete_table_create(uint32_t size);

gasnete_table_item_t * gasnete_table_search(const gasnete_table_t * const table, uint32_t key);

uint32_t gasnete_table_insert(gasnete_table_t * const table, gasnete_table_item_t item);

uint32_t gasnete_table_remove(gasnete_table_t * const table, uint32_t key, gasnete_table_item_t * deleted);

void gasnete_table_copy(const gasnete_table_t * const src, gasnete_table_t * const dst);

void gasnete_table_free(gasnete_table_t * const table);

#endif /* TABLE_H_ */
