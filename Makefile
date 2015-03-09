PARSER_CFLAGS	:= -I/usr/include/sparse
PARSER_LIBS	:= -lsparse -lboost_serialization
PARSER_OBJS	:= kabi.o checksum.o kabi-node.o
PARSER_HDRS	:= kabi.h checksum.h kabi-node.h

LOOKUP_CFLAGS	:= -std=gnu++11
LOOKUP_LIBS	:= -lboost_serialization
LOOKUP_OBJS	:= kabilookup.o kabi-node.o options.o error.o
LOOKUP_HDRS	:= kabilookup.h kabi-node.h options.h error.h

CXXFLAGS	+= -std=gnu++11

PROGRAMS=kabi-parser kabi-lookup

all	: $(PROGRAMS)

clean	:
	rm -vf *.o $(PROGRAMS)

kabi-parser	: $(PARSER_OBJS) $(PARSER_HDRS)
	g++ $(CXXFLAGS) $(PARSER_CFLAGS) -o kabi-parser $(PARSER_OBJS) $(PARSER_LIBS)

kabi-lookup 	: $(LOOKUP_OBJS) $(LOOKUP_HDRS)
	g++ $(CXXFLAGS) $(PARSER_CFLAGS) -o kabi-lookup $(LOOKUP_OBJS) $(LOOKUP_LIBS)

