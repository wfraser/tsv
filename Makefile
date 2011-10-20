#CFLAGS=-Wall -Werror -std=c99 -gstabs -O0
CFLAGS=-Wall -Werror -std=c99

all: tsv

tsv: main.o tsv.o growbuf.o

clean:
	rm -f tsv *.o

