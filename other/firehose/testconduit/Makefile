fh_dir = ..
gasnet_dir = ../../..

CC     = gcc
CFLAGS = -g3 -I. -I$(fh_dir) -I$(gasnet_dir) -I$(gasnet_dir)/extended-ref -DGASNET_PAR -DGASNET_DEBUG

.PHONY: clean

VPATH=.:$(fh_dir):$(gasnet_dir)

SOURCELIST = testconduit.c firehose.c firehose_hash.c firehose_page.c \
	     firehose_region.c gasnet_internal.c

OBJLIST = $(SOURCELIST:%.c=%.o)

all: testconduit

.c.o:
	$(CC) $(CFLAGS) -c $<

testconduit: $(OBJLIST)
	$(CC) -o testconduit $(OBJLIST)

clean:
	rm -rf $(OBJLIST) testconduit testconduit.exe

