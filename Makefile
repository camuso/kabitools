# kabitools Makefile 

# Need to have sparse headers handy in a local include directory
#
CFLAGS		+= -I./include -I/usr/include/c++/4.9.2
CXXFLAGS	+= -std=gnu++11

LIBS		+= -lboost_serialization
STATICLIBS	+= /work/kabi/lib/libsparse.a

COMMON_OBJS	:= checksum.o kabi-map.o
COMMON_HDRS	:= checksum.h kabi-map.h

PARSER_OBJS	:= $(COMMON_OBJS) kabi.o
PARSER_HDRS	:= $(COMMON_HDRS) kabi.h $

LOOKUP_OBJS	:= $(COMMON_OBJS) kabilookup.o options.o error.o rowman.o qrow.o
LOOKUP_HDRS	:= $(COMMON_HDRS) kabilookup.h options.h error.h rowman.h qrow.h

DUMP_OBJS	:= $(COMMON_OBJS) kabidump.o
DUMP_HDRS	:= $(COMMON_HDRS) kabidump.h

PROGRAMS = kabi-parser kabi-lookup kabi-dump

all	: $(PROGRAMS)

clean	:
	rm -vf *.o $(PROGRAMS)

kabi-parser	: $(PARSER_OBJS) $(PARSER_HDRS)
	g++ $(CXXFLAGS) $(CFLAGS) -o kabi-parser $(PARSER_OBJS) $(LIBS) $(STATICLIBS)

kabi-lookup 	: $(LOOKUP_OBJS) $(LOOKUP_HDRS)
	g++ $(CXXFLAGS) -o kabi-lookup $(LOOKUP_OBJS) $(LIBS)

kabi-dump	: $(DUMP_OBJS) $(DUMP_HDRS)
	g++ $(CXXFLAGS) -o kabi-dump $(DUMP_OBJS) $(LIBS)

