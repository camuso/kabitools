PARSER_CFLAGS	:= -I/usr/include/sparse
PARSER_LIBS	:= -lsparse -lboost_serialization
PARSER_OBJS	:= kabi.o checksum.o kabi-node.o
PARSER_HDRS	:= kabi.h checksum.h kabi-node.h

LOOKUP_CFLAGS	:= -std=gnu++11
LOOKUP_LIBS	:= -lboost_serialization
LOOKUP_OBJS	:= kabilookup.o kabi-node.o options.o error.o
LOOKUP_HDRS	:= kabilookup.h kabi-node.h options.h error.h

COMPDB_CFLAGS	:= -std=gnu++11
COMPDB_LIBS	:= -lboost_serialization
COMPDB_OBJS	:= kabi-compdb.o kabi-node.o
COMPDB_HDRS	:= kabi-compdb.h kabi-node.h

CXXFLAGS	+= -std=gnu++11

PROGRAMS=kabi-parser kabi-lookup kabi-compdb

all	: $(PROGRAMS)

clean	:
	rm -vf *.o $(PROGRAMS)

kabi-parser	: $(PARSER_OBJS) $(PARSER_HDRS)
	g++ $(CXXFLAGS) $(PARSER_CFLAGS) -o kabi-parser $(PARSER_OBJS) $(PARSER_LIBS)

kabi-lookup 	: $(LOOKUP_OBJS) $(LOOKUP_HDRS)
	g++ $(CXXFLAGS) $(LOOKUP_CFLAGS) -o kabi-lookup $(LOOKUP_OBJS) $(LOOKUP_LIBS)

kabi-compdb	: $(COMPDB_OBJS) $(COMPDB_HDRS)
	g++ $(CXXFLAGS) $(COMPDB_CFLAGS) -o kabi-compdb $(COMPDB_OBJS) $(COMPDB_LIBS)
