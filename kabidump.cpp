/* kabidump.cpp - class to dump .dat file created by kabi-parser
 *
 * Copyright (C) 2015  Red Hat Inc.
 * Tony Camuso <tcamuso@redhat.com>
 *
 * Boost serialization used by kabi-parser does not leave legible text files,
 * even though it is a text archive. There is no '\n' after each record.
 * This utility makes the data legible, and dumps the output to stdout.
 * Output can be redirected to a file by the user.
 *
 * This is free software. You can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

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

	if (kb_dump_dnodemap((char *)datafilename.c_str()) != 0)
		exit(1);
}

int main(int argc, char **argv)
{
	kabidump kd(argc, argv);

	return 0;
}
