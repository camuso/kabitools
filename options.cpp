#include <string>
#include <iostream>
#include "kabilookup.h"
#include "options.h"

using namespace std;

options::options()
{
	kb_flags = 0;
	longopts[OPT_NODUPS] = "no-dups";
	longopts[OPT_ARGS] = "args";
	longopts[OPT_LIST] = "list";
	longopts[OPT_DIR] = "dir";

	strparms[STR_LIST] = "./redhat/kabi/parser/kabi-files.list";
	strparms[STR_FILE] = "../kabi-data.dat";
}

int options::count_bits(unsigned mask)
{
	int count = 0;

	do {
		count += mask & 1;
	} while (mask >>= 1);

	return count;
}

// Check for mutually exclusive flags.
bool options::check_flags()
{
	if (kb_flags & KB_QUIET)
		kb_flags &= ~KB_VERBOSE;

	return !(count_bits(kb_flags & kb_exemask) > 1);
}

bool options::parse_long_opt(char *argstr, char ***argv)
{
	unsigned i;

	for (i = 0; i < OPT_COUNT; ++i)
		if(string(++argstr) == longopts[i])
			break;
	switch (i) {
	case OPT_NODUPS :
		kb_flags |= KB_NODUPS;
		break;
	case OPT_ARGS	:
		kb_flags |= KB_ARGS;
		break;
	case OPT_LIST	:
		kb_flags |= KB_LIST;
		kb_flags &= ~KB_FILE;
		strparms[STR_LIST] = *((*argv)++);
		break;
	case OPT_DIR	:
		kb_flags |= KB_DIR;
		strparms[STR_DIR] = *((*argv)++);
		break;
	default		:
		return false;
	}

	return true;
}

bool options::parse_opt(char opt, char ***argv)
{
	switch (opt) {
	case 'f' : strparms[STR_FILE] = *((*argv)++);
		   kb_flags |= KB_FILE;
		   kb_flags &= ~KB_LIST;
		   break;
	case 'c' : kb_flags |= KB_COUNT;
		   strparms[STR_DECL] = *((*argv)++);
		   break;
	case 'd' : kb_flags |= KB_DECL;
		   strparms[STR_DECL] = *((*argv)++);
		   break;
	case 'e' : kb_flags |= KB_EXPORTS;
		   strparms[STR_DECL] = *((*argv)++);
		   break;
	case 's' : kb_flags |= KB_STRUCT;
		   strparms[STR_DECL] = *((*argv)++);
		   break;
	case 'q' : kb_flags |= KB_QUIET;
		   kb_flags &= ~KB_VERBOSE;
		   break;
	case 'v' : kb_flags |= KB_VERBOSE;
		   kb_flags &= ~KB_QUIET;
		   break;
	case 'w' : kb_flags |= KB_WHOLE_WORD;
		   break;
	case 'h' : cout << lookup::get_helptext();
		   exit(0);
	default  : return false;
	}
	return true;
}

int options::get_options(int *idx, char **argv)
{
	int index = 0;
	char *argstr;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		argstr = &(*argv++)[1];

		if (*argstr == '-')
			if(parse_long_opt(argstr, &argv))
				continue;

		for (i = 0; argstr[i]; ++i)
			if (!parse_opt(argstr[i], &argv))
				return -1;
		if (!*argv)
			break;
	}

	*idx = index;
	return kb_flags;
}
