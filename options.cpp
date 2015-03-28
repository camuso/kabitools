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
}

bool options::parse_long_opt(char *argstr)
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
	default		:
		return false;
	}

	return true;
}

int options::get_options(int *idx, char **argv,
			 string &declstr, string &datafile)
{
	int index = 0;
	char *argstr;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		argstr = &(*argv++)[1];

		if (*argstr == '-')
			if(parse_long_opt(argstr))
				continue;

		for (i = 0; argstr[i]; ++i)
			if (!parse_opt(argstr[i], &argv, declstr, datafile))
				return -1;
		if (!*argv)
			break;
	}

	*idx = index;
	return kb_flags;
}

bool options::parse_opt(char opt, char ***argv, string &declstr, string &datafile)
{
	switch (opt) {
	case 'f' : datafile = *((*argv)++);
		   break;
	case 'c' : kb_flags |= KB_COUNT;
		   declstr = *((*argv)++);
		   break;
	case 'd' : kb_flags |= KB_DECL;
		   declstr = *((*argv)++);
		   break;
	case 'e' : kb_flags |= KB_EXPORTS;
		   declstr = *((*argv)++);
		   break;
	case 's' : kb_flags |= KB_STRUCT;
		   declstr = *((*argv)++);
		   break;
	case 'q' : kb_flags |= KB_QUIET;
		   break;
	case 'v' : kb_flags |= KB_VERBOSE;
		   break;
	case 'w' : kb_flags |= KB_WHOLE_WORD;
		   break;
	case 'h' : cout << lookup::get_helptext();
		   exit(0);
	default  : return false;
	}
	return true;
}
