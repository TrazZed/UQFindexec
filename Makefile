CC = gcc 
CFLAGS = -Wall -Wextra  -pedantic -std=gnu99 -g  -I/local/courses/csse2310/include -L/local/courses/csse2310/lib -lcsse2310a3

uqfindexec: uqfindexec.c
	$(CC) $(CFLAGS) -o $@ $<

clean: 
	rm uqfindexec
