/*  $Archive:: /Ti/AMMPI/ammpi_reqrep.c                                   $
 *     $Date: 2003/06/05 11:58:56 $
 * $Revision: 1.11 $
 * Description: AMMPI Implementations of request/reply operations
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */
#include <assert.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#ifndef WIN32
  #include <sys/time.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

#include <ammpi_internal.h>

/* forward decls */
static int AMMPI_RequestGeneric(ammpi_category_t category, 
                          ep_t request_endpoint, ammpi_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr,
                          uint8_t systemType, uint8_t systemArg);
static int AMMPI_ReplyGeneric(ammpi_category_t category, 
                          ammpi_buf_t *requestbuf, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr,
                          uint8_t systemType, uint8_t systemArg);
/*------------------------------------------------------------------------------------
 * Private helpers
 *------------------------------------------------------------------------------------ */
static int intpow(int val, int exp) {
  int retval = 1;
  int i;
  assert(exp >= 0);
  for (i = 0; i < exp; i++) retval *= val;
  return retval;
  }
/* ------------------------------------------------------------------------------------ */
#ifdef WIN32
  extern int64_t getMicrosecondTimeStamp() {
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
/* #elif defined(__I386__) 
 * TODO: it would be nice to take advantage of the Pentium's "rdtsc" instruction,
 * which reads a fast counter incremented on each cycle. Unfortunately, that
 * requires a way to convert cycles to microseconds, and there doesn't appear to 
 * be a way to directly query the cycle speed
 */

#else /* unknown processor - use generic UNIX call */
  extern int64_t getMicrosecondTimeStamp() {
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
/* ------------------------------------------------------------------------------------ */
/* mpihandle points to the MPI_Request to receive the non-blocking send handle, 
 * or null to use a blocking send
 */
static int sendPacket(ep_t ep, void *packet, int packetlength, en_t destaddress, MPI_Request *mpihandle) {
  int retval;
  assert(ep && packet && packetlength > 0);
  assert(packetlength <= AMMPI_MAX_NETWORK_MSG);

  #if DEBUG_VERBOSE
  { char temp[80];
    fprintf(stderr, "sending packet to (%s)\n", AMMPI_enStr(destaddress, temp)); fflush(stderr);
    }
  #endif

  #if AMMPI_NONBLOCKING_SENDS
    if (mpihandle && *mpihandle == MPI_REQUEST_NULL) {
      /* could also use synchronous mode non-blocking send here */
      retval = MPI_Isend(packet, packetlength, MPI_BYTE, destaddress.mpirank, destaddress.mpitag, *(ep->pmpicomm), mpihandle);
    } else
  #endif
    {
      retval = MPI_Bsend(packet, packetlength, MPI_BYTE, destaddress.mpirank, destaddress.mpitag, *(ep->pmpicomm));
    }
  if (retval != MPI_SUCCESS) 
     AMMPI_RETURN_ERRFR(RESOURCE, sendPacket, MPI_ErrorName(retval));        

  ep->stats.TotalBytesSent += packetlength;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
static int AMMPI_GetOpcode(int isrequest, ammpi_category_t cat) {
  switch (cat) {
    case ammpi_Short:
      if (isrequest) return AM_REQUEST_M;
      else return AM_REPLY_M;
    case ammpi_Medium:
      if (isrequest) return AM_REQUEST_IM;
      else return AM_REPLY_IM;
    case ammpi_Long:
      if (isrequest) return AM_REQUEST_XFER_M;
      else return AM_REPLY_XFER_M; 
    default: assert(0);
      return -1;
    }
  }
/* ------------------------------------------------------------------------------------ */
static int sourceAddrToId(ep_t ep, en_t sourceAddr) {
  /*  return source id in ep perproc table of this remote addr, or -1 for not found */
  ammpi_node_t i; 
  
  /* try the common case where mapping ids match MPI rank */
  if (sourceAddr.mpirank < ep->totalP &&
      enEqual(ep->perProcInfo[sourceAddr.mpirank].remoteName, sourceAddr)) 
    return sourceAddr.mpirank;

  /* failed - use linear search */
  for (i = 0; i < ep->totalP; i++) {
    if (enEqual(ep->perProcInfo[i].remoteName, sourceAddr))
      return i;
    }
  return -1;
  }
/* ------------------------------------------------------------------------------------ */
/* accessors for packet args, data and length
 * the only complication here is we want data to be double-word aligned, so we may add
 * an extra unused 4-byte argument to make sure the data lands on a double-word boundary
 */
#define HEADER_EVEN_WORDLENGTH  (((int)(uintptr_t)((&((ammpi_buf_t *)NULL)->_Data)-1))%8==0?1:0)
#define ACTUAL_NUM_ARGS(pMsg) (AMMPI_MSG_NUMARGS(pMsg)%2==0?       \
                            AMMPI_MSG_NUMARGS(pMsg)+!HEADER_EVEN_WORDLENGTH:  \
                            AMMPI_MSG_NUMARGS(pMsg)+HEADER_EVEN_WORDLENGTH)

#define GET_PACKET_LENGTH(pbuf)                                       \
  (((char *)&pbuf->_Data[4*ACTUAL_NUM_ARGS(&pbuf->Msg) + pbuf->Msg.nBytes]) - ((char *)pbuf))
#define PREDICT_PACKET_LENGTH(nArgs,nBytes)  /* conservative estimate of packet size */  \
  ((int)(uintptr_t)(char *)&(((ammpi_buf_t*)NULL)->_Data[4*(nArgs+1) + nBytes]))
#define GET_PACKET_DATA(pbuf)                                         \
  (&pbuf->_Data[4*ACTUAL_NUM_ARGS(&pbuf->Msg)])
#define GET_PACKET_ARGS(pbuf)                                         \
  ((uint32_t *)pbuf->_Data)
/* ------------------------------------------------------------------------------------ */
#define RUN_HANDLER_SHORT(phandlerfn, token, pArgs, numargs) do {                       \
  assert(phandlerfn);                                                                   \
  if (numargs == 0) (*(AMMPI_HandlerShort)phandlerfn)((void *)token);                   \
  else {                                                                                \
    uint32_t *args = (uint32_t *)(pArgs); /* eval only once */                          \
    switch (numargs) {                                                                  \
      case 1:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0]); break;         \
      case 2:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1]); break;\
      case 3:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2]); break; \
      case 4:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3]); break; \
      case 5:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4]); break; \
      case 6:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
      case 7:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
      case 8:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
      case 9:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
      case 10: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
      case 11: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
      case 12: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
      case 13: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
      case 14: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
      case 15: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
      case 16: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
      default: abort();                                                                 \
      }                                                                                 \
    }                                                                                   \
  } while (0)
/* ------------------------------------------------------------------------------------ */
#define _RUN_HANDLER_MEDLONG(phandlerfn, token, pArgs, numargs, pData, datalen) do {   \
  assert(phandlerfn);                                                         \
  if (numargs == 0) (*phandlerfn)(token, pData, datalen);                     \
  else {                                                                      \
    uint32_t *args = (uint32_t *)(pArgs); /* eval only once */                \
    switch (numargs) {                                                        \
      case 1:  (*phandlerfn)(token, pData, datalen, args[0]); break;           \
      case 2:  (*phandlerfn)(token, pData, datalen, args[0], args[1]); break;  \
      case 3:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2]); break; \
      case 4:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3]); break; \
      case 5:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4]); break; \
      case 6:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
      case 7:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
      case 8:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
      case 9:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
      case 10: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
      case 11: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
      case 12: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
      case 13: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
      case 14: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
      case 15: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
      case 16: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
      default: abort();                                                                 \
      }                                                                                 \
    }                                                                                   \
  } while (0)
#define RUN_HANDLER_MEDIUM(phandlerfn, token, pArgs, numargs, pData, datalen) do {      \
    assert(((uintptr_t)pData) % 8 == 0);  /* we guarantee double-word alignment for data payload of medium xfers */ \
    _RUN_HANDLER_MEDLONG((AMMPI_HandlerMedium)phandlerfn, (void *)token, pArgs, numargs, (void *)pData, (int)datalen); \
    } while(0)
#define RUN_HANDLER_LONG(phandlerfn, token, pArgs, numargs, pData, datalen)             \
  _RUN_HANDLER_MEDLONG((AMMPI_HandlerLong)phandlerfn, (void *)token, pArgs, numargs, (void *)pData, (int)datalen)
/* ------------------------------------------------------------------------------------ */
#ifdef DEBUG
  #define REFUSE_NOTICE(reason) ErrMessage("I just refused a message and returned to sender. Reason: %s", reason)
#else
  #define REFUSE_NOTICE(reason) (void)0
#endif

/* this is a local-use-only macro for AMMPI_ServiceIncomingMessages */
#define AMMPI_REFUSEMESSAGE(ep, buf, errcode) do {                         \
    int retval;                                                                 \
    buf->Msg.systemMessageType = (uint8_t)ammpi_system_returnedmessage;         \
    buf->Msg.systemMessageArg = (uint8_t)errcode;                               \
    retval = sendPacket(ep, buf, GET_PACKET_LENGTH(buf), (buf)->status.sourceAddr, NULL); \
       /* ignore errors sending this */                                         \
    if (retval != AM_OK) ErrMessage("failed to sendPacket to refuse message");  \
    else REFUSE_NOTICE(#errcode);                                               \
    goto donewithmessage;                                                       \
    } while(0)

/* main message receive workhorse - 
 * service available incoming messages, up to AMMPI_MAX_RECVMSGS_PER_POLL
 * note this is NOT reentrant - only one call to this method should be in progress at any time
 * if blockForActivity, then block until something happens
 * sets numUserHandlersRun to number of user handlers that got run
 */
extern int AMMPI_ServiceIncomingMessages(ep_t ep, int blockForActivity, int *numUserHandlersRun) {
  int i;
  
  if (!numUserHandlersRun) AMMPI_RETURN_ERR(BAD_ARG);
  *numUserHandlersRun = 0;

  for (i = 0; AMMPI_MAX_RECVMSGS_PER_POLL == 0 || i < AMMPI_MAX_RECVMSGS_PER_POLL; i++) {
    #if AMMPI_PREPOST_RECVS
      int idxready = 0;
    #else
      static ammpi_buf_t _recvBuffer;
    #endif  
    ammpi_buf_t *buf = NULL; /* the buffer that holds the incoming msg */
    MPI_Status mpistatus;

    if_pf (blockForActivity && *numUserHandlersRun > 0) return AM_OK; /* got one - done blocking */

    { /* check for message */
      int msgready = FALSE;

      #if AMMPI_PREPOST_RECVS
        #if AMMPI_MPIIRECV_ORDERING_WORKS
          /* according to the MPI spec we should be able to use a single request test/wait
           * fn if we keep track of the oldest recv initiated in the circular buffer, 
           * but some MPI implementations may get this subtlely wrong, so don't count on it
           */
          idxready = ep->rxCurr;
          assert(ep->rxHandle[idxready] != MPI_REQUEST_NULL);
          if_pf (blockForActivity) {
            MPI_SAFE(MPI_Wait(&(ep->rxHandle[idxready]), &mpistatus));
            msgready = TRUE;
          } else {
            MPI_SAFE(MPI_Test(&(ep->rxHandle[idxready]), &msgready, &mpistatus));
            if (!msgready) idxready = MPI_UNDEFINED;
          }
        #else
          if_pf (blockForActivity) {
            MPI_SAFE(MPI_Waitany(ep->rxNumBufs, ep->rxHandle, &idxready, &mpistatus));
            msgready = TRUE;
          }
          else {
            MPI_SAFE(MPI_Testany(ep->rxNumBufs, ep->rxHandle, &idxready, &msgready, &mpistatus));
          }
        #endif
        if (msgready) {
          assert(ep->rxHandle[idxready] == MPI_REQUEST_NULL);
          buf = &ep->rxBuf[idxready];
        }
        else assert(idxready == MPI_UNDEFINED);
      #else
        if_pf (blockForActivity) {
          MPI_SAFE(MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, *(ep->pmpicomm), &mpistatus));
          msgready = TRUE;
        }
        else {
          MPI_SAFE(MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, *(ep->pmpicomm), &msgready, &mpistatus));
        }
        if (msgready) buf = &_recvBuffer;
      #endif

      if (!msgready) return AM_OK; /* nothing else waiting */
    }
  
    /* we have a real message waiting - get it */
    { ammpi_bufstatus_t* status = &(buf->status); /* the status block for this buffer */
      ammpi_msg_t *msg = &(buf->Msg);
      int32_t sourceId; /* id in perProcInfo of sender */

      #if !AMMPI_PREPOST_RECVS
        MPI_SAFE(MPI_Recv(buf, AMMPI_MAX_NETWORK_MSG, MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, *(ep->pmpicomm), &mpistatus));
      #endif

      if_pf (mpistatus.MPI_TAG != ep->name.mpitag) {
        #if DEBUG
          fprintf(stderr,"Warning: AMMPI ignoring a stray MPI message (wrong MPI tag)...\n"); fflush(stderr);
        #endif
        goto donewithmessage;
      }

      { /* MPI-specific sanity checks */
        int recvlen;
        MPI_SAFE(MPI_Get_count(&mpistatus, MPI_BYTE, &recvlen));
        if_pf (recvlen > AMMPI_MAX_NETWORK_MSG)
          AMMPI_RETURN_ERRFR(RESOURCE, AMMPI_ServiceIncomingMessages, "buffer overrun - received message too long");
        if_pf (recvlen  < AMMPI_MIN_NETWORK_MSG)
          AMMPI_RETURN_ERRFR(RESOURCE, AMMPI_ServiceIncomingMessages, "incomplete message received");
      }

      /* remember which ep sent/recvd this message */
      status->sourceAddr.mpirank = mpistatus.MPI_SOURCE;
      status->dest = ep; 

      { /*  find the source id */
        int mpi_id = status->sourceAddr.mpirank;
        /* can't use sourceAddrToId because we don't know full en_t (remote mpitag) */
        if_pt (mpi_id < ep->totalP && /* first check common case where rank matches mapping */
          mpi_id == ep->perProcInfo[mpi_id].remoteName.mpirank) {
          sourceId = mpi_id;
          status->sourceAddr.mpitag = ep->perProcInfo[sourceId].remoteName.mpitag;
        } else { /* do linear search - leave as -1 if unfound */
          for (sourceId = ep->totalP-1; sourceId >= 0; sourceId--) {
            if (mpi_id == ep->perProcInfo[sourceId].remoteName.mpirank) break;
          }
          if (sourceId >= 0)
            status->sourceAddr.mpitag = ep->perProcInfo[sourceId].remoteName.mpitag;
        }
        status->sourceId = (ammpi_node_t)sourceId;
      }

      #if 0 && DEBUG_VERBOSE
      { char temp[80];
        printf("MPI_Recv got buflen=%i sourceAddr=%s\n", 
          retval, length, AMMPI_enStr(status->sourceAddr, temp));
        fflush(stdout);
        }
      #endif

      /* perform acceptance checks */
      { int numargs = AMMPI_MSG_NUMARGS(msg);
        int isrequest = AMMPI_MSG_ISREQUEST(msg);
        ammpi_category_t cat = AMMPI_MSG_CATEGORY(msg);
        int issystemmsg = ((ammpi_system_messagetype_t)msg->systemMessageType) != ammpi_system_user;

        /* handle returned messages */
        if_pf (issystemmsg) { 
          ammpi_system_messagetype_t type = ((ammpi_system_messagetype_t)msg->systemMessageType);
          if (type == ammpi_system_returnedmessage) { 
            AMMPI_HandlerReturned handlerfn = (AMMPI_HandlerReturned)ep->handler[0];
            op_t opcode;
            if (sourceId < 0) goto donewithmessage; /*  unknown source, ignore message */
            opcode = AMMPI_GetOpcode(isrequest, cat);

            /* note that source/dest for returned mesgs reflect the virtual "message denied" packet 
             * although it doesn't really matter because the AM2 spec is too vague
             * about the argblock returned message argument for it to be of any use to anyone
             */
            status->replyIssued = TRUE; /* prevent any reply */
            status->handlerRunning = TRUE;
            assert(handlerfn);
            (*handlerfn)(msg->systemMessageArg, opcode, (void *)buf);
            status->handlerRunning = FALSE;
            ep->stats.ReturnedMessages++;
            goto donewithmessage;
            }
          }

        if (isrequest) ep->stats.RequestsReceived[cat]++;
        else ep->stats.RepliesReceived[cat]++;

        if_pf (sourceId < 0) AMMPI_REFUSEMESSAGE(ep, buf, EBADENDPOINT);

      #if AMMPI_USE_AMTAGS
        if_pf (ep->tag == AM_NONE || 
           (ep->tag != msg->tag && ep->tag != AM_ALL))
            AMMPI_REFUSEMESSAGE(ep, buf, EBADTAG);
      #endif
        if_pf (ep->handler[msg->handlerId] == ammpi_unused_handler &&
            !issystemmsg && msg->handlerId != 0)
            AMMPI_REFUSEMESSAGE(ep, buf, EBADHANDLER);
      
        switch (cat) {
          case ammpi_Short:
            if_pf (msg->nBytes > 0 || msg->destOffset > 0)
              AMMPI_REFUSEMESSAGE(ep, buf, EBADLENGTH);
            break;
          case ammpi_Medium:
            if_pf (msg->nBytes > AMMPI_MAX_MEDIUM || msg->destOffset > 0)
              AMMPI_REFUSEMESSAGE(ep, buf, EBADLENGTH);
            break;
          case ammpi_Long: 
            /* check segment limits */
            if_pf (((uintptr_t)ep->segAddr + msg->destOffset) == 0 || ep->segLength == 0)
              AMMPI_REFUSEMESSAGE(ep, buf, EBADSEGOFF);
            if_pf (msg->destOffset + msg->nBytes > ep->segLength || msg->nBytes > AMMPI_MAX_LONG)
              AMMPI_REFUSEMESSAGE(ep, buf, EBADLENGTH);
            break;
          default:
            assert(0);
          }


        /* --- message accepted --- */
        #if AMMPI_COLLECT_LATENCY_STATS
          if (!isrequest) { 
            /* gather some latency statistics */
            uint64_t now = getMicrosecondTimeStamp();
            uint64_t latency = (now - desc->firstSendTime);
            ep->stats.RequestSumLatency += latency;
            if (latency < ep->stats.RequestMinLatency) ep->stats.RequestMinLatency = latency;
            if (latency > ep->stats.RequestMaxLatency) ep->stats.RequestMaxLatency = latency;
          }
        #endif

        { /*  run the handler */
          status->replyIssued = FALSE;
          status->handlerRunning = TRUE;
          if (ep->preHandlerCallback) ep->preHandlerCallback();
          if_pf (issystemmsg) { /* an AMMPI system message */
            ammpi_system_messagetype_t type = ((ammpi_system_messagetype_t)(msg->systemMessageType & 0xF));
            switch (type) {
              case ammpi_system_autoreply:
                /*  do nothing, already taken care of */
                break;
              case ammpi_system_controlmessage:
                /*  run a control handler */
                if (ep->controlMessageHandler == NULL || ep->controlMessageHandler == ammpi_unused_handler)
                  ErrMessage("got an AMMPI control message, but no controlMessageHandler is registered. Ignoring...");
                else {
                  RUN_HANDLER_SHORT(ep->controlMessageHandler, buf, 
                                    GET_PACKET_ARGS(buf), numargs);
                }
                break;
              default:
                assert(0);
              }
            }
          else { /* a user message */
            switch (cat) {
              case ammpi_Short: 
                RUN_HANDLER_SHORT(ep->handler[msg->handlerId], buf, 
                                  GET_PACKET_ARGS(buf), numargs);
                break;
              case ammpi_Medium: 
                RUN_HANDLER_MEDIUM(ep->handler[msg->handlerId], buf, 
                                   GET_PACKET_ARGS(buf), numargs, 
                                   GET_PACKET_DATA(buf), msg->nBytes);
                break;
              case ammpi_Long: {
                int8_t *pData = ((int8_t *)ep->segAddr) + msg->destOffset;
                /*  a single-message bulk transfer. do the copy */
                memcpy(pData, GET_PACKET_DATA(buf), msg->nBytes);
                RUN_HANDLER_LONG(ep->handler[msg->handlerId], buf, 
                                   GET_PACKET_ARGS(buf), numargs, 
                                   pData, msg->nBytes);
                break;
                }
              default:
                assert(0);
              }
            }
          if (ep->postHandlerCallback) ep->postHandlerCallback();
          status->handlerRunning = FALSE;
          (*numUserHandlersRun)++;

          if (isrequest && !status->replyIssued) {
            va_list va_dummy; va_list *p_dummy = &va_dummy; /* dummy value */
            /*  user didn't reply, so issue an auto-reply */
            if_pf (AMMPI_ReplyGeneric(ammpi_Short, buf, 0, 0, 0, 0, 0, va_dummy, ammpi_system_autoreply, 0) 
                != AM_OK) /*  should never happen - don't return here to prevent leaking buffer */
              ErrMessage("Failed to issue auto reply in AMMPI_ServiceIncomingMessages");
            }
          }
        } /*  acceptance checks */

      donewithmessage: /* message handled - continue to next one */
      #if AMMPI_PREPOST_RECVS
        /* repost the recv */
        assert(ep->rxHandle[idxready] == MPI_REQUEST_NULL);
        MPI_SAFE(MPI_Irecv(&ep->rxBuf[idxready], AMMPI_MAX_NETWORK_MSG, MPI_BYTE, 
                           MPI_ANY_SOURCE, MPI_ANY_TAG, *(ep->pmpicomm), 
                           &ep->rxHandle[idxready]));
        #if AMMPI_MPIIRECV_ORDERING_WORKS
          assert(idxready == ep->rxCurr);
          ep->rxCurr = ep->rxCurr + 1;
          if (ep->rxCurr >= ep->rxNumBufs) ep->rxCurr = 0;
        #endif
      #endif

      } /*  message waiting */
    }  /*  for */
  return AM_OK;
  } /*  AMMPI_ServiceIncomingMessages */
#undef AMMPI_REFUSEMESSAGE  /* this is a local-use-only macro */
/*------------------------------------------------------------------------------------
 * Poll
 *------------------------------------------------------------------------------------ */
extern int AM_Poll(eb_t eb) {
  int i;
  AMMPI_CHECKINIT();
  if_pf (!eb) AMMPI_RETURN_ERR(BAD_ARG);

  for (i = 0; i < eb->n_endpoints; i++) {
    int retval;
    ep_t ep = eb->endpoints[i];

    if_pt (ep->depth != -1) { /* only poll endpoints which have buffers */
      int userHandlersRun = 0;
      retval = AMMPI_ServiceIncomingMessages(ep, FALSE, &userHandlersRun); /* drain network and check for activity */
      if_pf (retval != AM_OK) AMMPI_RETURN(retval);
      }
    }

  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_Block(eb_t eb) {
  /* block until some endpoint receive buffer becomes non-empty with a valid user message
   * may poll, and does handle SPMD control events
   */
  int retval = AM_OK;
  if (eb->n_endpoints == 1) {
    int userHandlersRun = 0;
    while (retval == AM_OK && userHandlersRun == 0) {
      /* drain network and check for activity */
      retval = AMMPI_ServiceIncomingMessages(eb->endpoints[0], TRUE, &userHandlersRun); 
    }
  }
  else {
    /* we could implement this (at least for AMMPI_PREPOST_RECVS) by combining the handle vectors, 
     * but it doesn't seem worthwhile right now
     */
    ErrMessage("unimplemented: tried to AMMPI_Block on an endpoint-bundle containing multiple endpoints...");
    abort();
  }
  return retval;
}
/*------------------------------------------------------------------------------------
 * Generic Request/Reply
 *------------------------------------------------------------------------------------ */
#if !AMMPI_NONBLOCKING_SENDS
  static ammpi_buf_t stagingbuf;
#endif
static int AMMPI_RequestGeneric(ammpi_category_t category, 
                          ep_t request_endpoint, ammpi_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr, 
                          uint8_t systemType, uint8_t systemArg) {
  int packetlength;
  ammpi_buf_t *outgoingbuf;

  /*  always poll before sending a request */
  AM_Poll(request_endpoint->eb);

  {
  MPI_Request *mpihandle = NULL;
  #if AMMPI_NONBLOCKING_SENDS
    /*  acquire a free request buffer */
    int predictedsz = PREDICT_PACKET_LENGTH(numargs, nbytes);
    int retval = AMMPI_AcquireSendBuffer(request_endpoint, predictedsz, TRUE, &outgoingbuf, &mpihandle);
    if_pf (retval != AM_OK) AMMPI_RETURN(retval);
    assert(outgoingbuf && mpihandle && *mpihandle == MPI_REQUEST_NULL);
  #else
    outgoingbuf = &stagingbuf;
  #endif

  /*  setup message meta-data */
  { ammpi_msg_t *msg = &outgoingbuf->Msg;
    AMMPI_MSG_SETFLAGS(msg, TRUE, category, numargs);
    msg->destOffset = dest_offset;
    msg->handlerId = handler;
    msg->nBytes = (uint16_t)nbytes;
    msg->systemMessageType = systemType;
    msg->systemMessageArg = systemArg;
    }

  { /*  setup args */
    int i;
    uint32_t *args = GET_PACKET_ARGS(outgoingbuf);
    for (i = 0; i < numargs; i++) {
      args[i] = (uint32_t)va_arg(argptr, int); /* must be int due to default argument promotion */
      }
    }

  { /*  setup data */
    if (nbytes > 0) {
      memcpy(GET_PACKET_DATA(outgoingbuf), source_addr, nbytes);
      }
    }

  { /*  perform the send */
    en_t destaddress = request_endpoint->translation[reply_endpoint].name;
    int retval;
    packetlength = GET_PACKET_LENGTH(outgoingbuf);
    #if AMMPI_NONBLOCKING_SENDS
      assert(packetlength <= predictedsz);
    #endif
    #if AMMPI_USE_AMTAGS
      outgoingbuf->Msg.tag = request_endpoint->translation[reply_endpoint].tag;
    #endif
    retval = sendPacket(request_endpoint, outgoingbuf, packetlength, destaddress, mpihandle);
    if_pf (retval != AM_OK) AMMPI_RETURN(retval);
    }

  #if AMMPI_COLLECT_LATENCY_STATS
    { uint64_t now = getMicrosecondTimeStamp();
      outgoingdesc->firstSendTime = now;
    }
  #endif

  request_endpoint->stats.DataBytesSent[category] += sizeof(int) * numargs + nbytes;
  request_endpoint->stats.RequestsSent[category]++;
  return AM_OK;
  }
}
/* ------------------------------------------------------------------------------------ */
static int AMMPI_ReplyGeneric(ammpi_category_t category, 
                          ammpi_buf_t *requestbuf, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr,
                          uint8_t systemType, uint8_t systemArg) {
  ep_t ep;
  ammpi_node_t destP;
  ammpi_buf_t *outgoingbuf;

  destP = requestbuf->status.sourceId;
  ep = requestbuf->status.dest;

  /*  we don't poll within a reply because by definition we are already polling somewhere in the call chain */

  {
  MPI_Request *mpihandle = NULL;
  #if AMMPI_NONBLOCKING_SENDS
    /*  acquire a free request buffer */
    int predictedsz = PREDICT_PACKET_LENGTH(numargs, nbytes);
    int retval = AMMPI_AcquireSendBuffer(ep, predictedsz, FALSE, &outgoingbuf, &mpihandle);
    if_pf (retval != AM_OK) AMMPI_RETURN(retval);
    assert(outgoingbuf && mpihandle && *mpihandle == MPI_REQUEST_NULL);
  #else
    outgoingbuf = &stagingbuf;
  #endif

  /*  setup message meta-data */
  { ammpi_msg_t *msg = &outgoingbuf->Msg;
    AMMPI_MSG_SETFLAGS(msg, FALSE, category, numargs);
    msg->destOffset = dest_offset;
    msg->handlerId = handler;
    msg->nBytes = (uint16_t)nbytes;
    msg->systemMessageType = systemType;
    msg->systemMessageArg = systemArg;
    }

  { /*  setup args */
    int i;
    uint32_t *args = GET_PACKET_ARGS(outgoingbuf);
    for (i = 0; i < numargs; i++) {
      args[i] = (uint32_t)va_arg(argptr, int); /* must be int due to default argument promotion */
      }
    #if USE_CLEAR_UNUSED_SPACE
      for ( ; i < AMMPI_MAX_SHORT; i++) {
        args[i] = 0;
        }
    #endif
    }

  { /*  setup data */
    memcpy(GET_PACKET_DATA(outgoingbuf), source_addr, nbytes);
    #if 0 /* not necessary- we never send this stuff */
      #if USE_CLEAR_UNUSED_SPACE
        memset(&(GET_PACKET_DATA(outgoingbuf)[nbytes]), 0, AMMPI_MAX_LONG - nbytes);
      #endif
    #endif
    }

  { /*  perform the send */
    int packetlength = GET_PACKET_LENGTH(outgoingbuf);
    en_t destaddress = ep->perProcInfo[destP].remoteName;
    int retval;
    #if AMMPI_NONBLOCKING_SENDS
      assert(packetlength <= predictedsz);
    #endif
    #if AMMPI_USE_AMTAGS
      outgoingbuf->Msg.tag = ep->perProcInfo[destP].tag;
    #endif
    retval = sendPacket(ep, outgoingbuf, packetlength, destaddress, mpihandle);
    if_pf (retval != AM_OK) AMMPI_RETURN(retval);
    }

  /* outgoingdesc->seqNum = !(outgoingdesc->seqNum); */ /* this gets handled by AMMPI_ServiceIncomingMessages */
  requestbuf->status.replyIssued = TRUE;
  ep->stats.RepliesSent[category]++;
  ep->stats.DataBytesSent[category] += sizeof(int) * numargs + nbytes;
  return AM_OK;
  }
}
/*------------------------------------------------------------------------------------
 * Request
 *------------------------------------------------------------------------------------ */
extern int AMMPI_RequestVA(ep_t request_endpoint, ammpi_node_t reply_endpoint, handler_t handler, 
                         int numargs, va_list argptr) {
  AMMPI_CHECKINIT();
  if_pf (!request_endpoint) AMMPI_RETURN_ERR(BAD_ARG);
#ifndef __PGI
  /* work-around a broken pgcc */
  if_pf (handler >= AMMPI_MAX_NUMHANDLERS) AMMPI_RETURN_ERR(BAD_ARG);
#endif
  if_pf (request_endpoint->depth == -1) AMMPI_RETURN_ERR(NOT_INIT); /* it's an error to call before AM_SetExpectedResources */
  if_pf (reply_endpoint >= request_endpoint->translationsz ||
     !request_endpoint->translation[reply_endpoint].inuse) AMMPI_RETURN_ERR(BAD_ARG);
  assert(numargs >= 0 && numargs <= AMMPI_MAX_SHORT);

  /*  call the generic requestor */
  return AMMPI_RequestGeneric(ammpi_Short, 
                                request_endpoint, reply_endpoint, handler, 
                                NULL, 0, 0,
                                numargs, argptr,
                                ammpi_system_user, 0);
  }
extern int AMMPI_Request(ep_t request_endpoint, ammpi_node_t reply_endpoint, handler_t handler, 
                         int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMMPI_RequestVA(request_endpoint, reply_endpoint, handler, 
                                  numargs, argptr);
    va_end(argptr);
    return retval;
}
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_RequestIVA(ep_t request_endpoint, ammpi_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes,
                          int numargs, va_list argptr) {
  AMMPI_CHECKINIT();
  if_pf (!request_endpoint) AMMPI_RETURN_ERR(BAD_ARG);
#ifndef __PGI
  /* work-around a broken pgcc */
  if_pf (handler >= AMMPI_MAX_NUMHANDLERS) AMMPI_RETURN_ERR(BAD_ARG);
#endif
  if_pf (request_endpoint->depth == -1) AMMPI_RETURN_ERR(NOT_INIT); /* it's an error to call before AM_SetExpectedResources */
  if_pf (reply_endpoint >= request_endpoint->translationsz ||
     !request_endpoint->translation[reply_endpoint].inuse) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (!source_addr) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (nbytes < 0 || nbytes > AMMPI_MAX_MEDIUM) AMMPI_RETURN_ERR(BAD_ARG);
  assert(numargs >= 0 && numargs <= AMMPI_MAX_SHORT);

  /*  call the generic requestor */
  return AMMPI_RequestGeneric(ammpi_Medium, 
                                request_endpoint, reply_endpoint, handler, 
                                source_addr, nbytes, 0,
                                numargs, argptr,
                                ammpi_system_user, 0);
  }
extern int AMMPI_RequestI(ep_t request_endpoint, ammpi_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes,
                          int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMMPI_RequestIVA(request_endpoint, reply_endpoint, handler, 
                                  source_addr, nbytes,
                                  numargs, argptr);
    va_end(argptr);
    return retval;
}
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_RequestXferVA(ep_t request_endpoint, ammpi_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int async, 
                          int numargs, va_list argptr) {
  AMMPI_CHECKINIT();
  if_pf (!request_endpoint) AMMPI_RETURN_ERR(BAD_ARG);
#ifndef __PGI
  /* work-around a broken pgcc */
  if_pf (handler >= AMMPI_MAX_NUMHANDLERS) AMMPI_RETURN_ERR(BAD_ARG);
#endif
  if_pf (request_endpoint->depth == -1) AMMPI_RETURN_ERR(NOT_INIT); /* it's an error to call before AM_SetExpectedResources */
  if_pf (reply_endpoint >= request_endpoint->translationsz ||
     !request_endpoint->translation[reply_endpoint].inuse) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (!source_addr) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (nbytes < 0 || nbytes > AMMPI_MAX_LONG) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (dest_offset > AMMPI_MAX_SEGLENGTH) AMMPI_RETURN_ERR(BAD_ARG);
  assert(numargs >= 0 && numargs <= AMMPI_MAX_SHORT);
  {
    if (async) { /*  decide if we can satisfy request without blocking */
      /* it's unclear from the spec whether we should poll before an async failure,
       * but by definition the app must be prepared for handlers to run when calling this 
       * request, so it shouldn't cause anything to break, and the async request is more likely
       * to succeed if we do. so:
       */
      AM_Poll(request_endpoint->eb);

      /* too hard to compute whether this will block */
      ErrMessage("unimplemented: AMMPI_RequestXferAsyncM not implemented - use AMMPI_RequestXferM");
      abort();
    }
    /* perform the send */
    return AMMPI_RequestGeneric(ammpi_Long, 
                                  request_endpoint, reply_endpoint, handler, 
                                  source_addr, nbytes, dest_offset,
                                  numargs, argptr,
                                  ammpi_system_user, 0);
    }
  }
extern int AMMPI_RequestXfer(ep_t request_endpoint, ammpi_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int async, 
                          int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMMPI_RequestXferVA(request_endpoint, reply_endpoint, handler, 
                                  source_addr, nbytes, dest_offset,
                                  async,
                                  numargs, argptr);
    va_end(argptr);
    return retval;
}
/*------------------------------------------------------------------------------------
 * Reply
 *------------------------------------------------------------------------------------ */
extern int AMMPI_ReplyVA(void *token, handler_t handler, 
                       int numargs, va_list argptr) {
  ammpi_buf_t *requestbuf;

  AMMPI_CHECKINIT();
  if_pf (!token) AMMPI_RETURN_ERR(BAD_ARG);
#ifndef __PGI
  /* work-around a broken pgcc */
  if_pf (handler >= AMMPI_MAX_NUMHANDLERS) AMMPI_RETURN_ERR(BAD_ARG);
#endif
  assert(numargs >= 0 && numargs <= AMMPI_MAX_SHORT);

  { /*  semantic checking on reply (are we in a handler, is this the first reply, etc.) */
    requestbuf = (ammpi_buf_t *)token;
    if_pf (!AMMPI_MSG_ISREQUEST(&requestbuf->Msg)) AMMPI_RETURN_ERR(RESOURCE); /* token is not a request */
    if_pf (!requestbuf->status.handlerRunning) AMMPI_RETURN_ERR(RESOURCE); /* token is not for an active request */
    if_pf (requestbuf->status.replyIssued) AMMPI_RETURN_ERR(RESOURCE);     /* already issued a reply */
    if_pf (((ammpi_system_messagetype_t)requestbuf->Msg.systemMessageType) != ammpi_system_user) 
      AMMPI_RETURN_ERR(RESOURCE); /* can't reply to a system message (returned message) */
    }

  /*  call the generic replier */
  return AMMPI_ReplyGeneric(ammpi_Short, 
                                requestbuf, handler, 
                                NULL, 0, 0,
                                numargs, argptr,
                                ammpi_system_user, 0);
  }
extern int AMMPI_Reply(void *token, handler_t handler, 
                       int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMMPI_ReplyVA(token, handler, 
                                  numargs, argptr);
    va_end(argptr);
    return retval;
}
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_ReplyIVA(void *token, handler_t handler, 
                          void *source_addr, int nbytes,
                          int numargs, va_list argptr) {
  ammpi_buf_t *requestbuf;

  AMMPI_CHECKINIT();
  if_pf (!token) AMMPI_RETURN_ERR(BAD_ARG);
#ifndef __PGI
  /* work-around a broken pgcc */
  if_pf (handler >= AMMPI_MAX_NUMHANDLERS) AMMPI_RETURN_ERR(BAD_ARG);
#endif
  if_pf (!source_addr) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (nbytes < 0 || nbytes > AMMPI_MAX_MEDIUM) AMMPI_RETURN_ERR(BAD_ARG);
  assert(numargs >= 0 && numargs <= AMMPI_MAX_SHORT);

  { /*  semantic checking on reply (are we in a handler, is this the first reply, etc.) */
    requestbuf = (ammpi_buf_t *)token;
    if_pf (!AMMPI_MSG_ISREQUEST(&requestbuf->Msg)) AMMPI_RETURN_ERR(RESOURCE); /* token is not a request */
    if_pf (!requestbuf->status.handlerRunning) AMMPI_RETURN_ERR(RESOURCE); /* token is not for an active request */
    if_pf (requestbuf->status.replyIssued) AMMPI_RETURN_ERR(RESOURCE);     /* already issued a reply */
    if_pf (((ammpi_system_messagetype_t)requestbuf->Msg.systemMessageType) != ammpi_system_user) 
      AMMPI_RETURN_ERR(RESOURCE); /* can't reply to a system message (returned message) */
    }

  /*  call the generic replier */
  return AMMPI_ReplyGeneric(ammpi_Medium, 
                                requestbuf, handler, 
                                source_addr, nbytes, 0,
                                numargs, argptr,
                                ammpi_system_user, 0);
  }
extern int AMMPI_ReplyI(void *token, handler_t handler, 
                          void *source_addr, int nbytes,
                          int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMMPI_ReplyIVA(token, handler, 
                                  source_addr, nbytes,
                                  numargs, argptr);
    va_end(argptr);
    return retval;
}
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_SendControlMessage(ep_t from, en_t to, int numargs, ...) {
  int dest_endpoint_index = -1;

  AMMPI_CHECKINIT();
  if_pf (!from) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (numargs < 0 || numargs > AMMPI_MAX_SHORT) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (from->depth == -1) AMMPI_RETURN_ERR(NOT_INIT); /* it's an error to call before AM_SetExpectedResources */
  dest_endpoint_index = sourceAddrToId(from, to);
  if_pf (dest_endpoint_index == -1) AMMPI_RETURN_ERR(BAD_ARG); /* can only send to a mapped peer */

  { /*  call the generic requestor */
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMMPI_RequestGeneric(ammpi_Short, 
                                  from, dest_endpoint_index, 0, 
                                  NULL, 0, 0,
                                  numargs, argptr,
                                  ammpi_system_controlmessage, 0);
    va_end(argptr);
    return retval;
    }
}
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_ReplyXferVA(void *token, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr) {
  ammpi_buf_t *requestbuf;

  AMMPI_CHECKINIT();
  if_pf (!token) AMMPI_RETURN_ERR(BAD_ARG);
#ifndef __PGI
  /* work-around a broken pgcc */
  if_pf (handler >= AMMPI_MAX_NUMHANDLERS) AMMPI_RETURN_ERR(BAD_ARG);
#endif
  if_pf (!source_addr) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (nbytes < 0 || nbytes > AMMPI_MAX_LONG) AMMPI_RETURN_ERR(BAD_ARG);
  if_pf (dest_offset > AMMPI_MAX_SEGLENGTH) AMMPI_RETURN_ERR(BAD_ARG);
  assert(numargs >= 0 && numargs <= AMMPI_MAX_SHORT);

  { /*  semantic checking on reply (are we in a handler, is this the first reply, etc.) */
    requestbuf = (ammpi_buf_t *)token;
    if_pf (!AMMPI_MSG_ISREQUEST(&requestbuf->Msg)) AMMPI_RETURN_ERR(RESOURCE); /* token is not a request */
    if_pf (!requestbuf->status.handlerRunning) AMMPI_RETURN_ERR(RESOURCE); /* token is not for an active request */
    if_pf (requestbuf->status.replyIssued) AMMPI_RETURN_ERR(RESOURCE);     /* already issued a reply */
    if_pf (((ammpi_system_messagetype_t)requestbuf->Msg.systemMessageType) != ammpi_system_user) 
      AMMPI_RETURN_ERR(RESOURCE); /* can't reply to a system message (returned message) */
    }


  /*  call the generic replier */
  return AMMPI_ReplyGeneric(ammpi_Long, 
                                requestbuf, handler, 
                                source_addr, nbytes, dest_offset,
                                numargs, argptr,
                                ammpi_system_user, 0);
  }
extern int AMMPI_ReplyXfer(void *token, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMMPI_ReplyXferVA(token, handler, 
                                  source_addr, nbytes, dest_offset,
                                  numargs, argptr);
    va_end(argptr);
    return retval;
}
/* ------------------------------------------------------------------------------------ */
extern void AMMPI_DefaultReturnedMsg_Handler(int status, op_t opcode, void *token) {
  char *statusStr = "*unknown*";
  char *opcodeStr = "*unknown*";
  ammpi_buf_t *msgbuf = (ammpi_buf_t *)token;
  int numArgs = AMMPI_MSG_NUMARGS(&msgbuf->Msg);
  uint32_t *args = GET_PACKET_ARGS(msgbuf);
  char argStr[255];
  int i;

  #define STATCASE(name, desc) case name: statusStr = #name ": " desc; break;
  switch (status) {
    STATCASE(EBADARGS      , "Arguments to request or reply function invalid    ");
    STATCASE(EBADENTRY     , "X-lation table index selected unbound table entry ");
    STATCASE(EBADTAG       , "Sender's tag did not match the receiver's EP tag  "); 
    STATCASE(EBADHANDLER   , "Invalid index into the recv.'s handler table      "); 
    STATCASE(EBADSEGOFF    , "Offset into the dest-memory VM segment invalid    ");
    STATCASE(EBADLENGTH    , "Bulk xfer length goes beyond a segment's end      ");
    STATCASE(EBADENDPOINT  , "Destination endpoint does not exist               ");
    STATCASE(ECONGESTION   , "Congestion at destination endpoint                ");
    STATCASE(EUNREACHABLE  , "Destination endpoint unreachable                  ");
    STATCASE(EREPLYREJECTED, "Destination endpoint refused reply message        ");
    }
  #define OPCASE(name) case name: opcodeStr = #name; break;
  switch (opcode) {
    OPCASE(AM_REQUEST_M);
    OPCASE(AM_REQUEST_IM);
    OPCASE(AM_REQUEST_XFER_M);
    OPCASE(AM_REPLY_M);
    OPCASE(AM_REPLY_IM);
    OPCASE(AM_REPLY_XFER_M);
    }

  argStr[0] = '\0';
  for (i=0; i < numArgs; i++) {
    char tmp[20];
    sprintf(tmp, "0x%08x  ", args[i]);
    strcat(argStr, tmp);
    }
  { char temp1[80];
    char temp2[80];
  ErrMessage("An active message was returned to sender,\n"
             "    and trapped by the default returned message handler (handler 0):\n"
             "Error Code: %s\n"
             "Message type: %s\n"
             "Destination: %s (%i)\n"
             "Handler: %i\n"
             "Tag: %s\n"
             "Arguments(%i): %s\n"
             "Aborting...",
             statusStr, opcodeStr, 
             AMMPI_enStr(msgbuf->status.sourceAddr, temp1), msgbuf->status.sourceId,
             msgbuf->Msg.handlerId, 
             #if AMMPI_USE_AMTAGS
               AMMPI_tagStr(msgbuf->Msg.tag, temp2),
             #else
               "<AM tags disabled>",
             #endif
             numArgs, argStr);
    }
  abort();
  }
/* ------------------------------------------------------------------------------------ */
