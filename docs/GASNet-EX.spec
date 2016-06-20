// TODO-EX: much work is needed to make this suitable for an external audience

// A "handle" is an opaque scalar type
// - Sync operation: test/try/wait w/ one/all/some flavors
//   + Success consumes the handle
struct gasneti_handle_t;
typedef struct gasneti_handle_t *gasnetex_handle_t;

// Pre-defined values of type gasnetex_handle_t
// - GASNETEX_INVALID_HANDLE
// - GASNETEX_NO_OP_HANDLE
//   + Erroneous to pass this value to syncnb operations
#define GASNETEX_INVALID_HANDLE ((gasnetex_handle_t)(uintptr_t)0)
#define GASNETEX_NO_OP_HANDLE ((gasnetex_handle_t)(uintptr_t)1)

// An "lc_handle" is an opaque scalar type
// - "LC" is the *only* operation: test/try/wait w/ one/all/some flavors
//   + Success consumes the handle
//   + Success must precede sync of the gasnetex_handle_t (unless INVALID)
struct gasneti_lc_handle_s;
typedef struct gasneti_lc_handle_s *gasnetex_lc_handle_t;

// Pre-defined values of type gasnetex_lc_handle_t
#define GASNETEX_INVALID_LC_HANDLE ((gasnetex_lc_handle_t)0)

// Pre-defined values of type gasnetex_lc_handle_t*
// - GASNETEX_LC_INIT
//   + Pass to require completion within the initiation call
// - GASNETEX_LC_SYNC
//   + Pass to allow local completion as late as the sync call
// - GASNETEX_LC_GROUP
//   + Pass to NBI initiation calls to allow client to use NBI-based
//     "LC" calls to detect local completion
#define GASNETEX_LC_INIT  ((gasnetex_lc_handle_t*)(uintptr_t)1)
#define GASNETEX_LC_SYNC  ((gasnetex_lc_handle_t*)(uintptr_t)2)
#define GASNETEX_LC_GROUP ((gasnetex_lc_handle_t*)(uintptr_t)3)
// NOTE that '0' is intentionally excluded so NULL can be rejected

// A "team member" is an opaque scalar type
struct gasneti_team_member_s;
typedef struct gasneti_team_member_s *gasnetex_team_member_t;

// A "rank" is a position within a team
typedef uint32_t gasnetex_rank_t;

// Assume no more than 32 flags will be needed for any one family of calls
// However, flags to p2p initiation and segment creation (as examples) could overlap
typedef uint32_t gasnetex_flags_t;

// SOME flags values - certainly not complete.
// NOTE: Shifts of '999' are being used until the list is more fleshed out,
// and with the hope that a compiler warning about "shift larger than width"
// will ensure we fix them all eventually.
enum {
  //
  // Flags for point-to-point communication initiation
  //
      //
      // {SRC,DST}_OFFSET
      //
      // These flag bits indicate that the corresponding address argument
      // is an *offset* relative to the segment base.
      GASNETEX_FLAG_SRC_OFFSET = (1 << 999),
      GASNETEX_FLAG_DST_OFFSET = (1 << 999),
      //
      // IMMEDIATE
      //
      // This flag indicates that GASNet-EX *may* return without initiating
      // any communication if the conduit could determine that it would
      // need to block temporarily to obtain the necessary resources.  In
      // this case calls with return type 'gasnetex_handle_t' return
      // GASNETEX_NO_OP_HANDLE while those with return type 'int' will
      // return non-zero.
      //
      GASNETEX_FLAG_IMMEDIATE = (1 << 999),
      //
      // LC_COPY_{YES,NO}
      //
      // This mutually-exclusive pair of flags *may* override GASNet-EX's
      // choice of whether or not to make a copy of a payload (of a
      // non-blocking Put or AM) for the purpose of accelerating local
      // completion.  In the absence of these flags the conduit-specific
      // logic will apply.
      //
      // NOTE: these need more thought w.r.t. the implementation and
      // specification
      GASNETEX_FLAG_LC_COPY_YES = (1 << 999),
      GASNETEX_FLAG_LC_COPY_NO  = (1 << 999),
      //
      // {SRC,DST}_IN_SEGMENT
      //
      // These flag bits assert that for the coresponding source or
      // destination address the range of bytes [address, address+nbytes)
      // is contained within the union of current GASNet-EX segments.  In
      // the case of offset-based addressing the assertion is redundant,
      // but is still legal.
      GASNETEX_FLAG_SRC_IN_SEGMENT = (1 << 999),
      GASNETEX_FLAG_DST_IN_SEGMENT = (1 << 999),
};

// A "token" is an opaque scalar type
struct gasneti_token_s;
typedef struct gasneti_token_s *gasnetex_token_t;

// Handler index and argument types are fixed-width integers
typedef uint8_t gasnetex_handler_t;
typedef int32_t gasnetex_handlerarg_t;

// Widest scalar and width
typedef uintptr_t gasnetex_register_value_t;
#define GASNETEX_REGISTER_VALUE_T SIZEOF_VOID_P

// The following are the *internal* prototypes for AM Request and Reply
// The public API "instantiates" the "M" and the argument list.
// 
// NOTE 1: Return value
// 
//   An AM Request or Reply call is a "no op" IF AND ONLY IF the value
//   GASNETEX_FLAG_IMMEDIATE is included in the 'flags' argument AND the
//   conduit could determine that it would need to block temporarily to
//   obtain the necessary resources.  This case is distinguished by a
//   non-zero return.  In all other cases the return value is zero.  
//
//   In the "no op" case no communication has been performed and the
//   contents of the location named by the 'lc_opt' argument (if any) is
//   undefined.
//
// NOTE 2: The 'lc_opt' argument for local completion
//
//   The AM interfaces never detect or report remote completion, but do
//   have selectable behavior with respect to local completion (which
//   means that the payload buffer may safely by written, free()ed, etc).
//
//   Short AMs have no payload and therefore have no 'lc_opt' argument.
//
//   The Medium and Long Requests accept the pre-defined constant values
//   GASNETEX_LC_INIT and GASNETEX_LC_GROUP, and pointers to variables of
//   type 'gasnetex_lc_handle_t'.  The INIT constant requires that the
//   Request call not return until after local completion.  The GROUP
//   constant allows the Request call to return without delaying for local
//   completion and adds the AM operation to the set of operations for
//   which the "LCnbi" calls [NAMES TBD] will check local completion.  Use
//   of a pointer to a variable of type 'gasnetex_lc_handle_t' allows the
//   call to return without delay, and requres the client to use the "LCnb"
//   calls [NAMES TBD] to check local completion.
//
//   The 'lc_opt' argument to Medium and Long Reply calls behave as for the
//   Requests with the exception that GASNETEX_LC_GROUP is *not* permitted.
//   It is also important to note that it is not legal to "test", "try" or
//   "wait" on a 'gasnetex_lc_handle_t' in AM handler context.
//   [TBD: we *could* allow handlers to make bounded calls to "test", which
//   does not Poll, if we wanted to.]

// Long
extern int gasnetex_AMRequestLongM(
           gasnetex_team_member_t team,   // Names a local context ("return address")
           gasnetex_rank_t rank,          // Together with 'team', names a remote context
           gasnetex_handler_t handler,    // Index into handler table of remote context
           const void *source_addr,       // Payload address (or OFFSET)
           size_t nbytes,                 // Payload length
           void *dest_addr,               // Payload destination address (or OFFSET)
           gasnetex_lc_handle_t *lc_opt,  // Local completion control (see above)
           gasnetex_flags_t flags,        // Flags to control this operation
           int numargs, ...);             // Argument list (0..AMMaxArgs) as varargs
extern int gasnetex_AMReplyLongM(
           gasnetex_token_t token,        // Names local and remote contexts
           gasnetex_handler_t handler,
           const void *source_addr,
           size_t nbytes,
           void *dest_addr,
           gasnetex_lc_handle_t *lc_opt,
           gasnetex_flags_t flags,
           int numargs, ...);
// Medium
extern int gasnetex_AMRequestMediumM(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           gasnetex_handler_t handler,
           const void *source_addr,
           size_t nbytes,
           gasnetex_lc_handle_t *lc_opt,
           gasnetex_flags_t flags,
           int numargs, ...);
extern int gasnetex_AMReplyMediumM(
           gasnetex_token_t token,
           gasnetex_handler_t handler,
           const void *source_addr,
           size_t nbytes,
           gasnetex_lc_handle_t *lc_opt,
           gasnetex_flags_t flags,
           int numargs, ...);
// Short
extern int gasnetex_AMRequestShortM(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           gasnetex_handler_t handler, 
           gasnetex_flags_t flags,
           int numargs, ...);
extern int gasnetex_AMReplyShortM(
           gasnetex_token_t token,
           gasnetex_handler_t handler,
           gasnetex_flags_t flags,
           int numargs, ...);

// Extended API
//
// NOTE 1: Return value
//
//   An Extended API initiation call is a "no op" IF AND ONLY IF the value
//   GASNETEX_FLAG_IMMEDIATE is included in the 'flags' argument AND the
//   conduit could determine that it would need to block temporarily to
//   obtain the necessary resources.  The blocking and nbi calls return a
//   non-zero value *only* in the "no op" case, while the nb calls return
//   GASNETEX_NO_OP_HANDLE.
//
//   In the "no op" case no communication has been performed and the
//   contents of the location named by the 'lc_opt' argument (if any) is
//   undefined.
//
// NOTE 2a: The 'lc_opt' argument for local completion (NBI case)
//
//   Implicit-handle non-blocking Puts have an 'lc_opt' argument which
//   controls the behavior with respect to local completion.  The value can
//   be the pre-defined constants GASNETEX_LC_INIT, GASNETEX_LC_SYNC, or
//   GASNETEX_LC_GROUP.  The INIT constant requires that the call not
//   return until the operation is locally complete.  The SYNC constant
//   permits the call to return without delaying for local completion,
//   which may occur as late as in the call which syncs (retires) the
//   operation (which might be an syncnb call if this call is within an nbi
//   access region).  The GROUP constant allows the Request call to return
//   without delaying for local completion and adds the Put operation to
//   the set of operations for which the "LCnbi" calls [NAMES TBD] will
//   check local completion.
//
// NOTE 2b: The 'lc_opt' argument for local completion (NB case)
//
//   Explicit-handle non-blocking Puts have an 'lc_opt' argument which
//   controls the behavior with respect to local completion.  The value can
//   be the pre-defined constants GASNETEX_LC_INIT or GASNETEX_LC_SYNC, or
//   a pointer to a variable of type 'gasnetex_lc_handle_t'.  The INIT
//   constant requires that the call not return until the operation is
//   locally complete.  The SYNC constant permits the call to return
//   without delaying for local completion, which may occur as late as in
//   the call which syncs (retires) the returned handle.  Use of a pointer
//   to a variable of type 'gasnetex_lc_handle_t' allows the call to return
//   without delay, and requires that the client use the "LCnb" calls
//   [NAMES TBD] to check for local completion.
//
// [Some text for lc-must-precede-sync is still needed]

// Put
extern int gasnetex_put(
           gasnetex_team_member_t team,   // Names a local context ("return address")
           gasnetex_rank_t rank,          // Together with 'team', names a remote context
           void *dest,                    // Remote (destination) address (or OFFSET)
           const void *src,               // Local (source) address (or OFFSET)
           size_t nbytes,                 // Length of xfer
           gasnetex_flags_t flags);       // Flags to control this operation
extern int gasnetex_put_nbi(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           void *dest,
           const void *src,
           size_t nbytes,
           gasnetex_lc_handle_t *lc_opt,  // Local completion control (see above)
           gasnetex_flags_t flags);
extern gasnetex_handle_t gasnetex_put_nb(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           void *dest,
           const void *src,
           size_t nbytes,
           gasnetex_lc_handle_t *lc_opt,
           gasnetex_flags_t flags);

// Get
extern int gasnetex_get( // Returns non-zero *only* in "no op" case (IMMEDIATE flag)
           gasnetex_team_member_t team,   // Names a local context ("return address")
           void *dest,                    // Local (destination) address (or OFFSET)
           gasnetex_rank_t rank,          // Together with 'team', names a remote context
           void *src,                     // Remote (source) address (or OFFSET)
           size_t nbytes,                 // Length of xfer
           gasnetex_flags_t flags);       // Flags to control this operation
extern int gasnetex_get_nbi( // Returns non-zero *only* in "no op" case (IMMEDIATE flag)
           gasnetex_team_member_t team,
           void *dest,
           gasnetex_rank_t rank,
           void *src,
           size_t nbytes,
           gasnetex_flags_t flags);
extern gasnetex_handle_t gasnetex_get_nb(
           gasnetex_team_member_t team,
           void *dest,
           gasnetex_rank_t rank,
           void *src,
           size_t nbytes,
           gasnetex_flags_t flags);

// Value-based
extern gasnetex_register_value_t gasnetex_get_val(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           void *src,
           size_t nbytes,
           gasnetex_flags_t flags);
extern int gasnetex_put_val(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           void *dest,
           gasnetex_register_value_t value,
           size_t nbytes,
           gasnetex_flags_t flags);
extern int gasnetex_put_nbi_val(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           void *dest,
           gasnetex_register_value_t value,
           size_t nbytes,
           gasnetex_flags_t flags);
extern gasnetex_handle_t gasnetex_put_nb_val(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           void *dest,
           gasnetex_register_value_t value,
           size_t nbytes,
           gasnetex_flags_t flags);

// Sync operations
// The operation is indicated by the suffix
//  + _test: no Poll call is made, returns zero on success, and non-zero otherwise.
//  + _wait: Polls until success, no return`

// Sync of a single NB handle
// Success is defined as when the passed handle is complete.
int  gasnetex_test_syncnb (gasnetex_handle_t handle);
void gasnetex_wait_syncnb (gasnetex_handle_t handle);

// Sync of an NB handle array - "some"
// Success is defined as one or more handles have been completed, OR
// the input array contains only GASNETEX_INVALID_HANDLE.
// Completed handles, if any, are overwritten with GASNETEX_INVALID_HANDLE.
int  gasnetex_test_syncnb_some (gasnetex_handle_t *phandle, size_t numhandles);
void gasnetex_wait_syncnb_some (gasnetex_handle_t *phandle, size_t numhandles);

// Sync of an NB handle array - "all"
// Success is defined as all passed handles have been completed, OR
// the input array contains only GASNETEX_INVALID_HANDLE.
// Completed handles, if any, are overwritten with GASNETEX_INVALID_HANDLE.
int  gasnetex_test_syncnb_all (gasnetex_handle_t *phandle, size_t numhandles);
void gasnetex_wait_syncnb_all (gasnetex_handle_t *phandle, size_t numhandles);





// NOTE: these Max payload queries have not yet been "vetted".
// Cost of changing these (if implemented too soon) is relatively small.
// Expressing LC_* requires some NEW 'flags' bits (since lc_opt replaced the old ones).
// rank == GASNETEX_ALL_RANKS yields min-of-maxes
extern size_t gasnetex_AMMaxLongRequest(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           int numargs,
           gasnetex_flags_t flags);
extern size_t gasnetex_AMMaxLongReply(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           int numargs,
           gasnetex_flags_t flags);
extern size_t gasnetex_AMMaxMediumRequest(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           int numargs,
           gasnetex_flags_t flags);
extern size_t gasnetex_AMMaxMediumReply(
           gasnetex_team_member_t team,
           gasnetex_rank_t rank,
           int numargs,
           gasnetex_flags_t flags);

// Pre-defined constant used to apply a query to all ranks in the team
#define GASNETEX_ALL_RANKS (~(gasnetex_rank_t)0)

