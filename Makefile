CFLAGS 		+= -I/usr/include/sparse -I/usr/include/c++/4.9.2
CXXFLAGS	+= -std=gnu++11

LIBS		+= -lsparse -lboost_serialization

COMMON_OBJS	:= checksum.o kabi-map.o
COMMON_HDRS	:= checksum.h kabi-map.h

PARSER_OBJS	:= $(COMMON_OBJS) kabi.o
PARSER_HDRS	:= $(COMMON_HDRS) kabi.h $

LOOKUP_OBJS	:= $(COMMON_OBJS) kabilookup.o options.o error.o
LOOKUP_HDRS	:= $(COMMON_HDRS) kabilookup.h options.h error.h

# COMPDB_OBJS	:= kabi-compdb.o kabi-node.o
# COMPDB_HDRS	:= kabi-compdb.h kabi-node.h

DUMP_OBJS	:= $(COMMON_OBJS) kabidump.o
DUMP_HDRS	:= $(COMMON_HDRS) kabidump.h

PROGRAMS = kabi-parser kabi-lookup kabi-dump

all	: $(PROGRAMS)

clean	:
	rm -vf *.o $(PROGRAMS)

kabi-parser	: $(PARSER_OBJS) $(PARSER_HDRS)
	g++ $(CXXFLAGS) $(CFLAGS) -o kabi-parser $(PARSER_OBJS) $(LIBS)

kabi-lookup 	: $(LOOKUP_OBJS) $(LOOKUP_HDRS)
	g++ $(CXXFLAGS) -o kabi-lookup $(LOOKUP_OBJS) $(LIBS)

kabi-dump	: $(DUMP_OBJS) $(DUMP_HDRS)
	g++ $(CXXFLAGS) -o kabi-dump $(DUMP_OBJS) $(LIBS)

# kabi-compdb	: $(COMPDB_OBJS) $(COMPDB_HDRS)
#	g++ $(CXXFLAGS) -o kabi-compdb $(COMPDB_OBJS) $(LIBS)
