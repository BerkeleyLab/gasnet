/*  $Archive:: /Ti/AMUDP/apputils.h                                       $
 *     $Date: 2003/12/17 10:12:24 $
 * $Revision: 1.2 $
 * Description: Application utilities on AMUDP
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _APPUTILS_H
#define _APPUTILS_H

#ifdef WIN32
  #include <windows.h>  
  #define sleep(x) Sleep(1000*x)
#endif

#if !defined(DEBUG) && !defined(NDEBUG)
  #ifdef AMUDP_DEBUG
    #define DEBUG 1
  #else
    #define NDEBUG 1
  #endif
#endif

#ifndef VERBOSE
  #if AMUDP_DEBUG_VERBOSE || GASNET_DEBUG_VERBOSE
    #define VERBOSE 1
  #else
    #define VERBOSE 0
  #endif
#endif

#include <assert.h>
#include <stdlib.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifdef _MSC_VER
  #pragma warning(disable: 4127)
#endif

/* in a multi-threaded program, this would also include a lock */
#define AM_Safe(fncall) do {                \
  if ((fncall) != AM_OK) {                  \
    printf("Error calling: %s\n", #fncall); \
    exit(1);                                \
    }                                       \
  } while(0)

#define AM_PollBlock(eb) do {                       \
        AM_Safe(AM_SetEventMask(eb, AM_NOTEMPTY));  \
        AM_Safe(AM_WaitSema(eb));                   \
        AM_Safe(AM_Poll(eb));                       \
        } while (0)

/* app can define this before including to move our handlers */
#ifndef APPUTIL_HANDLER_BASE
  #define APPUTIL_HANDLER_BASE  100
#endif

/* call first to setup handlers for all app utils */
void setupUtilHandlers(ep_t activeep, eb_t activeeb);

void printGlobalStats();


#ifdef UETH
  #define getCurrentTimeMicrosec() ueth_getustime()
#else
  extern int64_t getCurrentTimeMicrosec();
#endif

#ifndef AMUDP_OMIT_READWRITE
uint32_t getWord(int proc, void *addr);
void putWord(int proc, void *addr, uint32_t val);

void readWord(void *destaddr, int proc, void *addr);
void readSync();

void writeWord(int proc, void *addr, uint32_t val);
void writeSync();
#endif

#endif
