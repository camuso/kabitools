#include <string>
#include <iostream>
#include "kabilookup.h"

using namespace std;
using namespace kabilookup;

void error::init(int argc, char **argv)
{
	m_orig_argc = argc;
	m_orig_argv = argv;
	errstr[EXE_ARG2BIG]  = "Too many arguments";
	errstr[EXE_ARG2SML]  = "Not enough arguments";
	errstr[EXE_CONFLICT] = "You entered conflicting switches";
	errstr[EXE_BADFORM]  = "Badly formed argument list";
	errstr[EXE_INVARG]   = "Invalid argument.";
	errstr[EXE_NOFILE]   = "Seeking \"%s\", but cannot open database"
			       " file %s\n";
	errstr[EXE_NOTFOUND] = "\"%s\" cannot be found in database file %s\n";
	errstr[EXE_2MANY]    = "Too many items match \"%s\" in database %s."
			       " Be more specific.\n";
}

void error::print_cmdline()
{
	int i = 0;
	for (i = 0; i < m_orig_argc; ++i)
		printf(" %s", m_orig_argv[i]);
}

#include <boost/format.hpp>
using boost::format;

void error::print_cmd_errmsg(int err, string declstr, string datafile)
{
	if ((1 << err) & m_errmask) {
		cout << format("\n%s. You typed ...\n  ") % errstr[err];
		print_cmdline();
		cout << "\nPlease read the help text below.\n"
		     << lookup::get_helptext();
	} else {
		cout << format(errstr[err]) % declstr % datafile;
	}
}
