CC=gcc
CFLAGS=-Wall -I. -pthread -lvirt

all: vcpu_scheduler

vcpu_scheduler: vcpu_scheduler.c
	$(CC) -o vcpu_scheduler vcpu_scheduler.c $(CFLAGS)


