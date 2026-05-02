# Minimal Makefile fixture for language parse-validation tests.

CC      ?= gcc
CFLAGS  ?= -Wall -O2
SOURCES := main.c util.c

include common.mk

all: demo

demo: $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

clean:
	rm -f demo

.PHONY: all clean
