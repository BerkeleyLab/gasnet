#ifdef FIREHOSE_REGION
int
fh_region_ispinned(gasnet_node_t node, uintptr_t addr, size_t len)
{
	return 0;
}

/* ##################################################################### */
/* LOCAL PINNING                                                         */
/* ##################################################################### */
firehose_private_t *
fh_acquire_local_region(firehose_region_t *region)
{
	return NULL;
}

void
fh_relase_local_region(firehose_request_t *request)
{
	return;
}

/* ##################################################################### */
/* REMOTE PINNING                                                        */
/* ##################################################################### */
firehose_private_t *
fh_acquire_remote_region(gasnet_node_t node, firehose_region_t *reg, 
		         firehose_completed_fn_t callback, void *context)
{
	return NULL;
}
void
fh_release_remote_region(firehose_request_t *request)
{
	return;
}
#endif
