
PARSER_CFLAGS	+= -I/usr/include/sparse
PARSER_LIBS	:= -lsparse
#PARSER_CFLAGS	+= -I/usr/include/sparksyms
#PARSER_LIBS	:= -lsparksyms
LOOKUP_CFLAGS	+=
LOOKUP_LIBS	:= -lsqlite3

CFLAGS		+= -I/usr/include/sparse
LIBS		+= -lsparse -lsqlite3

PROGRAMS=kabi-parser kabi-lookup
LIB_H=checksum.h
LIB_OBJS=kabi.o kabilookup.o checksum.o

CC = gcc
LD = gcc

LDFLAGS += -g

all	: $(PROGRAMS)

clean	:
	rm -vf *.o $(PROGRAMS)

kabi-parser	: kabi.c checksum.o kabi-serial.o
	g++ $(PARSER_CFLAGS) -o kabi-parser -x c kabi.c -x c checksum.c -x c++ kabi-serial.cpp $(PARSER_LIBS)

kabi-lookup : kabilookup.c
	$(CC) $(LOOKUP_CFLAGS) -o kabi-lookup kabilookup.c $(LOOKUP_LIBS)
