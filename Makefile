
PARSER_CFLAGS	+= "-I/usr/include/sparse"
PARSER_LIBS	:= "-lsparse"
LOOKUP_CFLAGS	+=
LOOKUP_LIBS	:= "-lsqlite3"

all	: kabi kabilookup

kabi 	: kabi.c
	cc $(PARSER_CFLAGS) -o kabi kabi.c $(PARSER_LIBS)

kabilookup : kabilookup.c
	cc $(LOOKUP_CFLAGS) -o kabilookup kabilookup.c $(LOOKUP_LIBS)
