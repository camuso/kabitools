
PARSER_CFLAGS	+= "-I/usr/include/sparse"
PARSER_LIBS	:= "-lsparse"
LOOKUP_CFLAGS	+=
LOOKUP_LIBS	:= "-lsqlite3"

all	: kabi-parser kabi-lookup

kabi-parser	: kabi.c
	cc $(PARSER_CFLAGS) -o kabi-parser kabi.c $(PARSER_LIBS)

kabi-lookup : kabilookup.c
	cc $(LOOKUP_CFLAGS) -o kabi-lookup kabilookup.c $(LOOKUP_LIBS)
