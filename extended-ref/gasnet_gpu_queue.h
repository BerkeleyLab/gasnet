/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_gpu_queue.h,v $
 * $Date: 2010/07/15 20:25:19 $
 * $Revision: 1.1.2.1 $
 *
 * Description: Implementation of doubly-linked queue (list)
 *
 * Note: This queue implementation is not thread-safe so queue
 * operations need to be protected by locks (mutexes) if the queue is
 * accessed by more than one threads.
 *
 * Yili Zheng
 * LBNL 2010
 */

#ifndef GASNET_GPU_QUEUE_H_
#define GASNET_GPU_QUEUE_H_

typedef struct _gasnete_qnode_t {
  void *data;
  struct _gasnete_qnode_t *next;
  struct _gasnete_qnode_t *prev;
} gasnete_qnode_t;

typedef struct _gasnete_queue_t {
  gasnete_qnode_t *head;
  gasnete_qnode_t *tail;
} gasnete_queue_t;

static inline gasnete_qnode_t * gasnete_queue_head(gasnete_queue_t *q)
{
  return q->head;
}

static inline gasnete_qnode_t * gasnete_queue_tail(gasnete_queue_t *q)
{
  return q->tail;
}

static inline gasnete_qnode_t * gasnete_qnode_new()
{
  gasnete_qnode_t *gasnete_qnode;
  gasnete_qnode = (gasnete_qnode_t *)gasneti_malloc(sizeof(gasnete_qnode_t));
  gasneti_assert(gasnete_qnode != NULL);
  return gasnete_qnode;
}


static inline void gasnete_qnode_free(gasnete_qnode_t *qnode)
{
  gasneti_free(qnode);
}

static inline gasnete_queue_t * gasnete_queue_new()
{
  gasnete_queue_t *q;
  q =  (gasnete_queue_t *)gasneti_malloc(sizeof(gasnete_queue_t));
  gasneti_assert(q != NULL);
  q->head = NULL;
  q->tail = NULL;
  return q;
}

static inline void gasnete_queue_free(gasnete_queue_t *queue)
{
  gasneti_free(queue);
}

static inline void gasnete_qnode_remove(gasnete_qnode_t *qnode)
{
  gasnete_qnode_t *mynext, *myprev;

  if (qnode == NULL)
    return;

  mynext = qnode->next;
  myprev = qnode->prev;

  if (mynext != NULL)
    mynext->prev = myprev;

  if (myprev != NULL)
    myprev->next = mynext;
}

static inline int gasnete_qnode_insert_to_next(gasnete_qnode_t *qnode, gasnete_qnode_t *newnode)
{
  gasnete_qnode_t *mynext;

  if (qnode == NULL || newnode == NULL)
    return 1; /* fail */

  mynext = qnode->next;
  if (mynext != NULL)
    mynext->prev = newnode;
  qnode->next = newnode;
  newnode->next = mynext;
  newnode->prev = qnode;

  return 0; /* success */
}

static inline int gasnete_qnode_insert_to_prev(gasnete_qnode_t *qnode, gasnete_qnode_t *newnode)
{
  gasnete_qnode_t *myprev;

  if (qnode == NULL || newnode == NULL)
    return 1; /* fail */

  myprev = qnode->prev;
  if (myprev != NULL)
    myprev->next = newnode;
  qnode->prev = newnode;
  newnode->next = qnode;
  newnode->prev = myprev;

  return 0; /* success */
}

static inline void gasnete_queue_insert_head(gasnete_queue_t *queue, void *data)
{
  gasnete_qnode_t *newnode;

  newnode = gasnete_qnode_new();
  newnode->data = data;

  if (queue->head == NULL) {
    /* first gasnete_queue node */
    queue->head = newnode;
    queue->tail = newnode;
    newnode->next = NULL;
    newnode->prev = NULL;
  } else {
      gasnete_qnode_insert_to_prev(queue->head, newnode);
      queue->head = newnode;
  }
}

static inline void gasnete_queue_insert_tail(gasnete_queue_t *queue, void *data)
{
  gasnete_qnode_t *newnode;

  newnode = gasnete_qnode_new();
  newnode->data = data;

  if (queue->tail == NULL) {
    /* first node */
    queue->head = newnode;
    queue->tail = newnode;
    newnode->next = NULL;
    newnode->prev = NULL;
  } else {
      gasnete_qnode_insert_to_next(queue->tail, newnode);
      queue->tail = newnode;
  }

}

static inline void *gasnete_queue_remove_head(gasnete_queue_t *queue)
{
  gasnete_qnode_t *head = queue->head;
  void *data;

  if (head == NULL)
    return NULL;

  if (head == queue->tail) {
    /* last node */
    queue->head = NULL;
    queue->tail = NULL;
  } else {
    queue->head = head->next;
  }

  data = head->data;
  gasnete_qnode_remove(head); 
  gasnete_qnode_free(head); 

  return data;
}

static inline void *gasnete_queue_remove_tail(gasnete_queue_t *queue)
{
  gasnete_qnode_t *tail = queue->tail;
  void *data;

  if (tail == NULL)
    return NULL;

  if (tail == queue->head) {
    /* last gasnete_queue node */
    queue->tail = NULL;
    queue->head = NULL;
  } else {
    queue->tail = tail->prev;
  }
  
  data = tail->data;
  gasnete_qnode_remove(tail);
  gasnete_qnode_free(tail);

  return data;
}

static inline void *gasnete_queue_remove_node(gasnete_queue_t *queue, gasnete_qnode_t *qnode)
{
  void *data;
  
  if (qnode == queue->head)
    data = gasnete_queue_remove_head(queue);
  else if (qnode == queue->tail)
    data = gasnete_queue_remove_tail(queue);
  else {
    data = qnode->data;
    gasnete_qnode_remove(qnode);
    gasnete_qnode_free(qnode);
  }
  
  return data;
}


static inline int gasnete_queue_is_empty(gasnete_queue_t *queue)
{
  gasneti_assert(queue != NULL);
  return (queue->head == NULL);
}

#define gasnete_queue_enqueue gasnete_queue_insert_tail
#define gasnete_queue_dequeue gasnete_queue_remove_head
#define gasnete_queue_steal gasnete_queue_remove_tail
#define gasnete_queue_push gasnete_queue_insert_head
#define gasnete_queue_pop gasnete_queue_remove_head

#endif /* GASNET_GPU_QUEUE_H_ */
