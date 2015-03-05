PARSER_CFLAGS	+= -I/usr/include/sparse
PARSER_LIBS	:= -lsparse -lboost_serialization
PARSER_OBJS	:= kabi.o checksum.o kabi-node.o kabi-serial.o

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

kabi-parser	: $(PARSER_OBJS)
	 g++ -o kabi-parser $(PARSER_OBJS) $(PARSER_LIBS)

kabi-lookup : kabilookup.c
	$(CC) $(LOOKUP_CFLAGS) -o kabi-lookup kabilookup.c $(LOOKUP_LIBS)
