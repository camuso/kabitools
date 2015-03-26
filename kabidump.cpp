#include <boost/lexical_cast.hpp>
#include "kabidump.h"

//#define NDEBUG
#if !defined(NDEBUG)
#define DBG(x) x
#define RUN(x)
#define prdbg(fmt, ...) \
do { \
       printf(fmt, ##__VA_ARGS__); \
} while (0)
#else
#define DBG(x)
#define RUN(x) x
#define prdbg(fmt, ...)
#endif

using namespace std;
using namespace boost;

kabidump::kabidump(int argc, char **argv)
{
	string datafilename;

	// skip over argv[0], which is the invocation of this app.
	++argv; --argc;

	if (argc)
		datafilename = *argv;
	else
		datafilename = m_filename;

	if (kb_dump_cqnmap((char *)datafilename.c_str()) != 0)
		exit(1);
}

int main(int argc, char **argv)
{
	kabidump kd(argc, argv);

	return 0;
}
