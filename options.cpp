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
	return !(count_bits(kb_flags & kb_exemask) > 1);
}

bool options::parse_long_opt(char *argstr, char ***argv)
{
	unsigned i;

	for (i = 0; i < OPT_COUNT; ++i)
		if(string(argstr) == longopts[i])
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
		if (**argv == NULL) return false;
		strparms[STR_LIST] = *((*argv)++);
		break;
	case OPT_DIR	:
		kb_flags |= KB_DIR;
		if (**argv == NULL) return false;
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
	case 'f' : if (**argv == NULL) return false;
		   strparms[STR_FILE] = *((*argv)++);
		   kb_flags |= KB_FILE;
		   kb_flags &= ~KB_LIST;
		   break;
	case 'c' : if (**argv == NULL) return false;
		   strparms[STR_DECL] = *((*argv)++);
		   kb_flags |= KB_COUNT;
		   break;
	case 'd' : if (**argv == NULL) return false;
		   strparms[STR_DECL] = *((*argv)++);
		   kb_flags |= KB_DECL;
		   break;
	case 'e' : if (**argv == NULL) return false;
		   strparms[STR_DECL] = *((*argv)++);
		   kb_flags |= KB_EXPORTS;
		   break;
	case 's' : if (**argv == NULL) return false;
		   strparms[STR_DECL] = *((*argv)++);
		   kb_flags |= KB_STRUCT;
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
	int index;
	char *argstr;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		argstr = &(*argv++)[1];

		if (*argstr == '-')
			if(parse_long_opt(++argstr, &argv))
				goto chkargv;

		for (i = 0; argstr[i]; ++i)
			if (!parse_opt(argstr[i], &argv))
				return -1;
chkargv:
		if (!*argv)
			break;
	}

	*idx = index;
	return check_flags() ? kb_flags : -1;
}
