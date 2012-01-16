CFLAGS=-Wall -Werror -std=c99
CC=gcc

OBJS=main.o tsv.o growbuf.o csvformat.o

all: tsv

.SUFFIXES:

%.o: %.c
	@echo "    CC  $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

tsv: $(OBJS)
	@echo "  LINK  $<"
	@$(CC) $(LDFLAGS) -o $@ $(OBJS)

clean:
	@echo " CLEAN"
	@rm -f tsv *.o

