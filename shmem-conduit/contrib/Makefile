CC=cc
CFLAGS=-O0  -h ssp
#CFLAGS=-O

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

#all: shmem_min shmalloc_find
all: shmem_ovhd_cray
#all: shmem_core_elan

shmem_min: shmem_min.o
	$(CC) -o $@ shmem_min.o -lshmem

# Only for Cray
shmalloc_find: shmalloc_find.o
	$(CC) -o $@ shmalloc_find.o -lshmem

clean:
	rm -rf shmalloc_find.o shmem_min.o shmem_core.o shmem_core_elan shmem_core_cray


shmem_core_elan: shmem_core.o
	$(CC) -o $@ shmem_core.o -lshmem

shmem_core_cray: shmem_core.o
	$(CC) -h ssp -o $@ shmem_core.o -lsma

shmem_ovhd_cray: shmem_ovhd.o
	$(CC) -h ssp -o $@ shmem_ovhd.o -lsma

