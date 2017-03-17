#   $Source: bitbucket.org:berkeleylab/gasnet.git/docs/Makefile $
# Description: Makefile for GASNet documentation
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

# The GASNet specification in distributed in 3 primary formats (text, html and postscript)
# note that older version of makeinfo and TeX may have trouble correctly generating gasnet.ps

all: gasnet.txt gasnet.html gasnet.ps gasnet.pdf

# ------------------------------------------------------------------------------------
# TeX Postscript
gasnet.dvi: gasnet.texi texinfo.tex
	texi2dvi -c -q gasnet.texi

gasnet.ps: gasnet.dvi
	rm -f gasnet.ps
	dvips -f -t letter gasnet.dvi > gasnet.ps

# ------------------------------------------------------------------------------------
# Adobe PDF
gasnet.pdf: gasnet.texi texinfo.tex
	rm -f gasnet.pdf
	texi2dvi -c -p -o gasnet.pdf gasnet.texi

gasnet2.pdf: gasnet.dvi
	rm -f gasnet2.pdf
	dvipdf gasnet.dvi 

# ------------------------------------------------------------------------------------
# Text
gasnet.txt: gasnet.texi texinfo.tex
	makeinfo --no-headers --no-validate gasnet.texi > gasnet.txt
# ------------------------------------------------------------------------------------
# Monolithic HTML
gasnet.html: gasnet.texi texinfo.tex
	makeinfo --html --no-split --no-headers --no-validate gasnet.texi > gasnet.html
# ------------------------------------------------------------------------------------
# Segmented HTML (not really supported)
gasnet: gasnet.texi texinfo.tex
	makeinfo --html --no-validate gasnet.texi
# ------------------------------------------------------------------------------------
# info file (not really supported)
gasnet.info: gasnet.texi texinfo.tex
	makeinfo gasnet.texi

info: gasnet.info
	info -f gasnet.info
# ------------------------------------------------------------------------------------
unix:
	txt2unix gasnet.texi
# ------------------------------------------------------------------------------------
temp-clean:
	rm -f gasnet.dvi gasnet.ky gasnet.pg gasnet.toc gasnet.tmp gasnet.tp gasnet.vr gasnet.pg gasnet.log gasnet.cp gasnet.aux gasnet.fn gasnet.fns gasnet.cps

clean: temp-clean
	rm -f gasnet.info gasnet.html gasnet.ps gasnet.txt gasnet.pdf gasnet2.pdf
	rm -Rf gasnet

dist-hook: all temp-clean
	rm -f texinfo.tex gasnet.texi Makefile pshm-design.txt
	@: # Use "blessed" GASNet docs if specified
	@if test -n "$(GASNET_DOCS)"; then \
	   list=`cd $(GASNET_DOCS) && echo gasnet.*`; for f in $$list; do \
	     if test -f $$f; then rm -f $$f; fi; \
	     cmd="cp -p $(GASNET_DOCS)/$$f . && chmod a=r $$f"; \
	     echo $$cmd; eval $$cmd || exit $$?; \
	   done; \
	 fi

.PHONY: unix info gasnet clean
