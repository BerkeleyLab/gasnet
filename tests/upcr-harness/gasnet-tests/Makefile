CONDUIT_DIR=$(TOP_BUILDDIR)/gasnet/$(NETWORK)-conduit

test%-seq: force
	@$(MAKE) -C $(CONDUIT_DIR) $@
	cp -f $(CONDUIT_DIR)/`basename $@ -seq` $@

test%-par: force
	@$(MAKE) -C $(CONDUIT_DIR) $@
	cp -f $(CONDUIT_DIR)/`basename $@ -par` $@

test%-parsync: force
	@$(MAKE) -C $(CONDUIT_DIR) $@
	cp -f $(CONDUIT_DIR)/`basename $@ -parsync` $@

testconduitspecific: force
	@if test "$(NETWORK)" = "mpi" ; then 	\
           conduit_test=testmpi ;		\
           conduit_mode=par ;			\
         fi ;					\
	 if test "$(NETWORK)" = "udp" ; then 	\
           conduit_test=testcxx ;		\
           conduit_mode=par ;			\
         fi ;					\
	 if test "$$conduit_test" = "" ; then 	\
           conduit_test=testam ;		\
           conduit_mode=parsync ;		\
         fi ;					\
         $(MAKE) -C $(CONDUIT_DIR) $$conduit_test-$$conduit_mode && \
	 cp -f $(CONDUIT_DIR)/$$conduit_test $@

force:

.PHONY: force

