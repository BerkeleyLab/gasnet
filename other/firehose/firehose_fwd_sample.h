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

