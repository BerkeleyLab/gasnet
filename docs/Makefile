#   $Source: bitbucket.org:berkeleylab/gasnet.git/docs/Makefile $
# Description: Makefile for GASNet documentation
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

# The GASNet specification in distributed in 3 primary formats (text, html and postscript)
# note that older version of makeinfo and TeX may have trouble correctly generating gasnet.ps

OUTPUTS = gasnet.pdf gasnet.txt gasnet.html
all: $(OUTPUTS)
	md5sum $(OUTPUTS) || exit 0

SOURCES = gasnet.texi texinfo.tex Makefile

PERL = perl

# ------------------------------------------------------------------------------------
# TeX Postscript
gasnet.dvi: $(SOURCES)
	texi2dvi -c -q $<

gasnet.ps: gasnet.dvi
	rm -f $@
	dvips -f -t letter $< > $@

# ------------------------------------------------------------------------------------
# Adobe PDF
gasnet.pdf: $(SOURCES)
	texi2dvi -c -p -o $@ $<
	touch $@

gasnet2.pdf: gasnet.dvi
	dvipdf $<

# ------------------------------------------------------------------------------------
# Text
# Perl removes function category cruft that isn't properly elided in Plaintext fmt
gasnet.txt: $(SOURCES)
	rm -f $@
	makeinfo --no-headers --no-validate $< > $@
	$(PERL) -i.bak -pe 's/^ -- :/ /' $@
	rm -f $@.bak

# ------------------------------------------------------------------------------------
# Monolithic HTML
# Perl removes function category cruft that isn't properly elided in HTML fmt
# Also remove useless HTML jump table in Function index
INDEX_NAME=Function-Macro-and-Type-Index_fn_letter-G
gasnet.html: $(SOURCES)
	rm -f $@
	makeinfo --html --no-split --no-headers --no-validate $< > $@
	$(PERL) -i.bak -pe 's@^(<dt><a name="[^"]+"></a>): <em>@\1 <em>@' $@
	$(PERL) -0777 -i.bak -pe 's@<table><tr><t[^>]*>Jump to:[^<]*</t.><td><a [^>]*href="#$(INDEX_NAME)">.*?</table>@@msg ; s@<tr><t.><a name="$(INDEX_NAME)">.*?</tr>@@g' $@
	rm -f $@.bak
# ------------------------------------------------------------------------------------
# Segmented HTML (not really supported)
gasnet: $(SOURCES)
	makeinfo --html --no-validate $<
# ------------------------------------------------------------------------------------
# info file (not really supported)
gasnet.info: $(SOURCES)
	makeinfo $<

info: gasnet.info
	info -f gasnet.info
# ------------------------------------------------------------------------------------
temp-clean:
	rm -f gasnet.dvi gasnet.ky gasnet.pg gasnet.toc gasnet.tmp gasnet.tp gasnet.vr gasnet.pg gasnet.log gasnet.cp gasnet.aux gasnet.fn gasnet.fns gasnet.cps

clean: temp-clean
	rm -f gasnet.info gasnet.html gasnet.ps gasnet.txt gasnet.pdf gasnet2.pdf
	rm -Rf gasnet

dist-hook: temp-clean
	rm -f $(SOURCES) Makefile pshm-design.txt
	@: # Use "blessed" GASNet docs if specified
	@if test -n "$(GASNET_DOCS)"; then \
	   for f in $(OUTPUTS); do \
	     if test -f $(GASNET_DOCS)/$$f; then rm -f $$f; fi; \
	     cmd="cp -p $(GASNET_DOCS)/$$f . && chmod a=r $$f"; \
	     echo $$cmd; eval $$cmd || exit $$?; \
	   done; \
	 fi
	@for f in $(OUTPUTS); do \
	   if test -f $$f ; then \
	      md5sum $$f ; \
	   else \
	     echo "WARNING: $$f not found in source directory or GASNET_DOCS='$(GASNET_DOCS)' : spec document omitted" ; \
	   fi ; \
	 done

.PHONY: unix info gasnet clean
