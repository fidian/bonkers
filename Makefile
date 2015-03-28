CC=gcc
CCOPTS=-Wall -O2
LIBS=-lusb-1.0

all: bonkers

bonkers: bonkers.c
	$(CC) $(CCOPTS) -o $@ $< $(LIBS)

clean:
	[ -e bonkers ] && rm bonkers
