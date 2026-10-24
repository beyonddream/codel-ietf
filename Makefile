CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS ?= -lm

all: codel_sim

codel_sim: codel_sim.c codel.c codel.h
	$(CC) $(CFLAGS) -o $@ codel_sim.c codel.c $(LDLIBS)

run: codel_sim
	./codel_sim

clean:
	rm -f codel_sim
