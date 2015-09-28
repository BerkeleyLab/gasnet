CONDUIT_DIR=$(TOP_BUILDDIR)/gasnet/$(NETWORK)-conduit
STRICT_PROTO_FLAGS=`echo $$BUPC_STRICT_PROTO_FLAGS | sed -e 's/-Wc,//'`

testbarrierconf_%-seq: force
	@$(MAKE) -C $(CONDUIT_DIR) testbarrierconf-seq MANUAL_CFLAGS="$(STRICT_PROTO_FLAGS)"
	cp -f $(CONDUIT_DIR)/testbarrierconf$(EXESUFFIX) $@$(EXESUFFIX)

testbarrierconf_%-par: force
	@$(MAKE) -C $(CONDUIT_DIR) testbarrierconf-par MANUAL_CFLAGS="$(STRICT_PROTO_FLAGS)"
	cp -f $(CONDUIT_DIR)/testbarrierconf$(EXESUFFIX) $@$(EXESUFFIX)

test%-seq: force
	@$(MAKE) -C $(CONDUIT_DIR) $@ MANUAL_CFLAGS="$(STRICT_PROTO_FLAGS)"
	cp -f $(CONDUIT_DIR)/`basename $@ -seq`$(EXESUFFIX) $@$(EXESUFFIX)

test%-par: force
	@$(MAKE) -C $(CONDUIT_DIR) $@ MANUAL_CFLAGS="$(STRICT_PROTO_FLAGS)"
	cp -f $(CONDUIT_DIR)/`basename $@ -par`$(EXESUFFIX) $@$(EXESUFFIX)

test%-parsync: force
	@$(MAKE) -C $(CONDUIT_DIR) $@ MANUAL_CFLAGS="$(STRICT_PROTO_FLAGS)"
	cp -f $(CONDUIT_DIR)/`basename $@ -parsync`$(EXESUFFIX) $@$(EXESUFFIX)

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
         $(MAKE) -C $(CONDUIT_DIR) $$conduit_test-$$conduit_mode MANUAL_CFLAGS="$(STRICT_PROTO_FLAGS)" && \
	 cp -f $(CONDUIT_DIR)/$$conduit_test$(EXESUFFIX) $@$(EXESUFFIX)

parsync: force
	@$(MAKE) -C $(CONDUIT_DIR) $@
	@echo '#!/bin/sh' > $@ ; chmod +x $@

check-exports: force
	@$(MAKE) -C $(CONDUIT_DIR) $@
	@echo '#!/bin/sh' > $@ ; chmod +x $@

check-tools-exports: force
	@$(MAKE) -C $(TOP_BUILDDIR)/gasnet check-exports
	@echo '#!/bin/sh' > $@ ; chmod +x $@

check-libupcr-exports: force
	@$(MAKE) -C $(TOP_BUILDDIR) check-exports
	@echo '#!/bin/sh' > $@ ; chmod +x $@

force:

.PHONY: force

