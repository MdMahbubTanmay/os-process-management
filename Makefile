CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2

all: procmon

procmon: src/procmon.c
	$(CC) $(CFLAGS) -o $@ $<

test: procmon
	./tests/run_tests.sh

clean:
	rm -f procmon logs/*.txt
