fh_dir = ..
gasnet_dir = ../../..

CC     = gcc -pthread
CFLAGS = -I. -I$(fh_dir) -I$(gasnet_dir) -I$(gasnet_dir)/extended-ref -DGASNET_PAR -DGASNET_DEBUG -DGASNET_STATS -DGASNET_TRACE

.PHONY: clean

VPATH=.:$(fh_dir):$(gasnet_dir)

SOURCELIST = testconduit.c workload_gups.c firehose.c firehose_hash.c firehose_page.c \
	     firehose_region.c gasnet_internal.c 

OBJLIST = $(SOURCELIST:%.c=%.o)

all: testconduit

workload_gups.o:
	$(CC) $(CFLAGS) -c workload_gups.c

.c.o:
	$(CC) $(CFLAGS) -g -c $<

testconduit: $(OBJLIST)
	$(CC) -g -o testconduit $(OBJLIST)

clean:
	rm -rf $(OBJLIST) testconduit testconduit.exe

