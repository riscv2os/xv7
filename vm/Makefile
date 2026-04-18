CC ?= cc
CFLAGS = -Wall -O2 -std=c11
LDFLAGS =

# macOS / Linux portable
UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
	CFLAGS += -Wno-deprecated-declarations
endif

vm: vm.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o vm vm.c

clean:
	rm -f vm

.PHONY: clean
