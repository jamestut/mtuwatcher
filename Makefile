CFLAGS ?= -O2 -Wall -Wextra -arch arm64 -arch x86_64

mtuwatcher: mtuwatcher.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f mtuwatcher

.PHONY: clean
