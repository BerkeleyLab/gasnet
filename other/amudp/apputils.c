/*  $Archive:: /Ti/AMUDP/apputils.c                                       $
 *     $Date: 2003/12/22 08:48:32 $
 * $Revision: 1.2 $
 * Description: Application utilities on AMUDP
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <amudp.h>
#include <amudp_spmd.h>
#include "apputils.h"

/*  init by setupUtilHandlers */
static ep_t ep = NULL;
static eb_t eb = NULL;

/* all handler indices are defined here and registered at bottom of file */

#define STATS_REQ_HANDLER     (APPUTIL_HANDLER_BASE+0)

#define GET_REQ_HANDLER       (APPUTIL_HANDLER_BASE+1)
#define GET_REP_HANDLER       (APPUTIL_HANDLER_BASE+2)
#define PUT_REQ_HANDLER       (APPUTIL_HANDLER_BASE+3)
#define PUT_REP_HANDLER       (APPUTIL_HANDLER_BASE+4)

#define READ_REQ_HANDLER      (APPUTIL_HANDLER_BASE+5)
#define READ_REP_HANDLER      (APPUTIL_HANDLER_BASE+6)
#define WRITE_REQ_HANDLER     (APPUTIL_HANDLER_BASE+7)
#define WRITE_REP_HANDLER     (APPUTIL_HANDLER_BASE+8)


/* ------------------------------------------------------------------------------------ */
/*  statistics dump */
/* ------------------------------------------------------------------------------------ */
static int statscalls = 0;
static amudp_stats_t globalStats;
static void stats_request_handler(void *token, void *buf, int nbytes, int32_t procnum) {
  assert(nbytes == sizeof(amudp_stats_t));
  AM_Safe(AMUDP_AggregateStatistics(&globalStats, (amudp_stats_t *)buf));
  statscalls++;
  }

void printGlobalStats() {
  amudp_stats_t stats;
  statscalls = 0; 
  globalStats = AMUDP_initial_stats;

  AM_Safe(AMUDP_SPMDBarrier()); /* make sure we're done sending msgs for now */
  AM_Safe(AMUDP_GetEndpointStatistics(ep, &stats)); /* get statistics */
  AM_Safe(AMUDP_SPMDBarrier()); /* don't let stats msgs interfere */

  assert(sizeof(amudp_stats_t) < AM_MaxMedium());
  AM_Safe(AM_RequestI1(ep, 0, STATS_REQ_HANDLER, &stats, sizeof(amudp_stats_t), AMUDP_SPMDMyProc())); /* send to zero */

  if (AMUDP_SPMDMyProc() == 0) {
    while (statscalls < AMUDP_SPMDNumProcs()) {
      AM_Safe(AM_SetEventMask(eb, AM_NOTEMPTY));
      AM_Safe(AM_WaitSema(eb));
      AM_Safe(AM_Poll(eb));
      }
    fprintf(stderr, "--------------------------------------------------\n"
                    "Global stats:\n");
    AMUDP_DumpStatistics(stderr, &globalStats, 1);
    fprintf(stderr, "--------------------------------------------------\n");
    fflush(stderr);
    sleep(1); /* HACK: give a little time for this output to reach master */
    }

  AM_Safe(AMUDP_SPMDBarrier()); /* just to keep things clean */

  }
/* ------------------------------------------------------------------------------------ */
#ifndef UETH
  #ifdef WIN32
    int64_t getCurrentTimeMicrosec() {
      static int status = -1;
      static double multiplier;
      if (status == -1) { /*  first time run */
        LARGE_INTEGER freq;
        if (!QueryPerformanceFrequency(&freq)) status = 0; /*  don't have high-perf counter */
        else {
          multiplier = 1000000 / (double)freq.QuadPart;
          status = 1;
          }
        }
      if (status) { /*  we have a high-performance counter */
        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);
        return (int64_t)(multiplier * count.QuadPart);
        }
      else { /*  no high-performance counter */
        /*  this is a millisecond-granularity timer that wraps every 50 days */
        return (GetTickCount() * 1000);
        }
      }
  #else
    int64_t getCurrentTimeMicrosec() {
      int64_t retval;
      struct timeval tv;
      if (gettimeofday(&tv, NULL)) {
        perror("gettimeofday");
        abort();
        }
      retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
      return retval;
      }
  #endif
#endif
/* ------------------------------------------------------------------------------------ */
/*  synchronous gets and puts */
static void get_reply_handler(void *token, int ctr, int dest, int val) {
  uint32_t *pctr;
  uint32_t *pdest;
  
  pctr = (uint32_t *)ctr;
  pdest = (uint32_t *)dest;
  assert(pctr);
  assert(pdest);
  *pdest = (uint32_t)val;
  *pctr = TRUE;
  }

static void get_request_handler(void *token, int ctr, int dest, int addr) {
  uint32_t *paddr;

  paddr = (uint32_t *)addr;
  assert(paddr);

  AM_Safe(AM_Reply3(token, GET_REP_HANDLER, 
                    ctr, dest, *paddr));
  }

uint32_t getWord(int proc, void *addr) {
  volatile uint32_t getdone = FALSE;
  volatile uint32_t getval = 0;
  AM_Safe(AM_Request3(ep, proc, GET_REQ_HANDLER, 
                      (int)&getdone, (int)&getval, (int)addr));
  while (!getdone) AM_PollBlock(eb);
  return getval;
  }
/* ------------------------------------------------------------------------------------ */
static void put_reply_handler(void *token, int ctr) {
  uint32_t *pctr;

  pctr = (uint32_t *)ctr;
  assert(pctr);
  *pctr = TRUE;
  }

static void put_request_handler(void *token, int ctr, int dest, int val) {
  uint32_t *paddr;

  paddr = (uint32_t *)dest;
  assert(paddr);
  *paddr = (uint32_t)val;

  AM_Safe(AM_Reply1(token, PUT_REP_HANDLER, 
                    ctr));
  }

void putWord(int proc, void *addr, uint32_t val) {
  volatile uint32_t putdone = FALSE;
  AM_Safe(AM_Request3(ep, proc, PUT_REQ_HANDLER, 
                      (int)&putdone, (int)addr, (int)val));
  while (!putdone) AM_PollBlock(eb);
  return;
  }
/* ------------------------------------------------------------------------------------ */
/*  asynchronous reads and writes */
static volatile uint32_t readCtr = 0;
static void read_reply_handler(void *token, int ctr, int dest, int val) {
  uint32_t *pctr;
  uint32_t *pdest;

  pctr = (uint32_t *)ctr;
  pdest = (uint32_t *)dest;
  assert(pctr);
  assert(pdest);
  *pdest = (uint32_t)val;
  (*pctr)--;
  }

static void read_request_handler(void *token, int ctr, int dest, int addr) {
  uint32_t *paddr;

  paddr = (uint32_t *)addr;
  assert(paddr);

  AM_Safe(AM_Reply3(token, READ_REP_HANDLER, 
                    ctr, dest, *paddr));
  }


void readWord(void *destaddr, int proc, void *addr) {
  AM_Safe(AM_Request3(ep, proc, READ_REQ_HANDLER, 
                      (int)&readCtr, (int)destaddr, (int)addr));
  readCtr++;
  return;
  }

void readSync() {
  while (readCtr) AM_PollBlock(eb);
  }
/* ------------------------------------------------------------------------------------ */
static volatile uint32_t writeCtr = 0;
static void write_reply_handler(void *token, int ctr) {
  uint32_t *pctr;

  pctr = (uint32_t *)ctr;
  assert(pctr);
  (*pctr)--;
  }

static void write_request_handler(void *token, int ctr, int dest, int val) {
  uint32_t *paddr;

  paddr = (uint32_t *)dest;
  assert(paddr);
  *paddr = (uint32_t)val;

  AM_Safe(AM_Reply1(token, WRITE_REP_HANDLER, 
                    ctr));
  }

void writeWord(int proc, void *addr, uint32_t val) {
  AM_Safe(AM_Request3(ep, proc, WRITE_REQ_HANDLER, 
                      (int)&writeCtr, (int)addr, (int)val));
  writeCtr++;
  return;
  }

void writeSync() {
  while (writeCtr) AM_PollBlock(eb);
  }
/* ------------------------------------------------------------------------------------ */
void setupUtilHandlers(ep_t activeep, eb_t activeeb) {
  assert(activeep && activeeb);
  ep = activeep;
  eb = activeeb;

  AM_Safe(AM_SetHandler(ep, STATS_REQ_HANDLER, stats_request_handler));

  AM_Safe(AM_SetHandler(ep, GET_REQ_HANDLER, get_request_handler));
  AM_Safe(AM_SetHandler(ep, GET_REP_HANDLER, get_reply_handler));
  AM_Safe(AM_SetHandler(ep, PUT_REQ_HANDLER, put_request_handler));
  AM_Safe(AM_SetHandler(ep, PUT_REP_HANDLER, put_reply_handler));

  AM_Safe(AM_SetHandler(ep, READ_REQ_HANDLER, read_request_handler));
  AM_Safe(AM_SetHandler(ep, READ_REP_HANDLER, read_reply_handler));
  AM_Safe(AM_SetHandler(ep, WRITE_REQ_HANDLER, write_request_handler));
  AM_Safe(AM_SetHandler(ep, WRITE_REP_HANDLER, write_reply_handler));

  }
/* ------------------------------------------------------------------------------------ */
