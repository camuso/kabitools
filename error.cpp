#include <string>
#include <iostream>
#include "kabilookup.h"

using namespace std;

void error::map_err(int err, string str)
{
	pair<int, string> errpair = make_pair(err, str);
	m_errmap.insert(errpair);
}

void error::init(int argc, char **argv)
{
	m_orig_argc = argc;
	m_orig_argv = argv;
	map_err(EXE_ARG2BIG,  "Too many arguments");
	map_err(EXE_ARG2SML,  "Not enough arguments");
	map_err(EXE_CONFLICT, "You entered conflicting switches");
	map_err(EXE_BADFORM,  "Badly formed argument list");
	map_err(EXE_INVARG,   "Invalid argument.");
	map_err(EXE_NOFILE,   "Cannot open %s : %s\n");
	map_err(EXE_NOTFOUND, "Symbol \e[1m%s\e[0m is not in the graph.\n"
	                      "It is either kABI-safe or does not exist.\n");
	map_err(EXE_NOTWHITE, "\e[1m%s\e[0m : symbol is not whitelisted.\n");
	map_err(EXE_NO_WLIST, "No white list. Run \"make rh-kabi\"\n");
	map_err(EXE_NODIR,    "Cannot access directory: %s\n");
}

void error::print_cmdline()
{
	int i = 0;
	for (i = 0; i < m_orig_argc; ++i)
		printf(" %s", m_orig_argv[i]);
}

#include <boost/format.hpp>
using boost::format;

void error::print_errmsg(int err, std::vector<std::string> strvec)
{
	if (err == 0) {
		return;
	}
	else if ((1 << err) & m_cmderrmask) {
		cout << format("\n%s. You typed ...\n  ") % m_errmap.at(err);
		print_cmdline();
		cout << "\nPlease read the help text below.\n"
		     << lookup::get_helptext();
		return;
	} else {
		string& errstr = m_errmap.at(err);

		switch (strvec.size()) {
		case 0: cout << errstr;
			break;
		case 1: cout << format(errstr)	% strvec[0];
			break;
		case 2: cout << format(errstr)	% strvec[0] % strvec[1];
			break;
		case 3: cout << format(errstr)	% strvec[0] % strvec[1]
						% strvec[2];
			break;
		default : cout << errstr;;
		}
	}
}
