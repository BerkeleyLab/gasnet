# $Id: Makefile,v 1.1.2.1 2003/10/11 14:22:41 bonachea Exp $
# Copyright (c) 1997-, Dolphin Interconnect Solutions AS. All Rights Reserved.

# Makefile for syscall USER PROGS, unix.    Author: jwb

COMPILER = gcc
F = test
CCFLAGS = -g -DUNIX -DDMA_PULL_SUPPORT  
IFLAGS = -I/opt/DIS_Linux_2.4.20-8smp_170903/lib/api -I/opt/DIS_Linux_2.4.20-8smp_170903/include 
#LINK = /opt/DIS_Linux_2.4.20-8smp_170903/lib/demolib.a 
LINK = /opt/DIS_Linux_2.4.20-8smp_170903/lib/api/libsisci.a 

${F}: ${F}.c
	${COMPILER} ${CCFLAGS} ${F}.c  -c -o ${F}.o ${IFLAGS}
	${COMPILER} -c gasnet_core_internal.c -o gasnet_core_internal.o ${IFLAGS}
	${COMPILER} ${CCFLAGS} ${F}.o gasnet_core_internal.o -lpthread -o ${F} ${LINK}

clean: 
	rm -rf *.o 

