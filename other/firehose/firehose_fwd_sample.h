/* firehose_fwd.h: Firehose forward declarations */
/* At least one of the next two firehose impementations must be defined */
#define FIREHOSE_REGION
#define FIREHOSE_PAGE

/* Define the next preprocessor directive to allow firehose clients to
 * attach a client type in opaque firehose_region_t */
#define FIREHOSE_CLIENT_T

#ifdef FIREHOSE_CLIENT_T
typedef
struct _firehose_client_t {
	int	conduit_key;		/* conduit-specific */
}
firehose_client_t;
#endif

/* Connection-oriented pinning networks
 *
 * Some networks actually need to connect/disconnect to remote
 * segments through their own API calls.  For these networks, the
 * client must execute additional function calls for regions to be
 * pinned and unpinned.
 *
 * The firehose interface defines two preprocessor directives:
 *   FIREHOSE_BIND_CALLBACK allows a client to bind to a region to be
 *                          pinned in a network-specific manner;
 *   FIREHOSE_UNBIND_CALLBACK allows a client to unbind from a
 *                            region in a network-specific manner;
 */

/* Define the next preprocessor directive to allow the client to bind
 * to newly pinned regions once a move request completes locally.
 *
 * If active, this callback runs with the newly pinned regions once
 * the node initiating the move request receives the target's node's
 * reply. 
 */
#undef FIREHOSE_BIND_CALLBACK

/* Define the next preprocessor directive to allow the client to
 * unbind to regions once the firehose interface selects the region
 * for unpinning.
 *
 * If active, this callback runs with the regions selected for
 * unpinning and the target node the regions were mapped to.
 */
#undef FIREHOSE_UNBIND_CALLBACK

