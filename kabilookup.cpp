/* kabi-lookup.cpp - search the graph generated by kabi-parser for symbols
 *                   given by the user
 *
 * Copyright (C) 2015  Red Hat Inc.
 * Tony Camuso <tcamuso@redhat.com>
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

#include <cstring>
#include <fstream>
#include <boost/format.hpp>

#include "checksum.h"
#include "kabilookup.h"

using namespace std;
using boost::format;

#define NDEBUG
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

string lookup::get_helptext()
{
	return "\
\n\
kabi-lookup -[vw] -c|d|e|s symbol [-b datafile]\n\
    Searches a kabi database for symbols. The results of the search \n\
    are printed to stdout and indented hierarchically.\n\
\n\
    -c symbol   - Counts the instances of the symbol in the kabi tree. \n\
    -s symbol   - Prints to stdout every exported function that is implicitly \n\
                  or explicitly affected by the symbol. In verbose mode, the \n\
                  chain from the exported function to the symbol is printed.\n\
                  It is advisable to use \"-c symbol\" first. \n\
    -e symbol   - Specific to EXPORTED functions. Prints the function, \n\
                  and its argument list. With the -v verbose switch, it \n\
                  will print all the descendants of nonscalar arguments. \n\
    -d symbol   - Seeks a data structure and prints its members to stdout. \n\
                  The -v switch prints descendants of nonscalar members. \n\
    -v          - verbose lists all descendants of a symbol. \n\
    -w          - whole words only, default is \"match any and all\" \n\
    -f filelist - Optional list of data files that were created by kabi-parser. \n\
                  The default list created by running kabi-data.sh is \n\
		  \"./redhat/kabi/parser/kabi-files.list\" relative to the \n\
                  top of the kernel tree. \n\
    --no-dups   - A structure can appear more than once in the nest of an \n\
                  exported function, for example a pointer back to itself as\n\
                  parent to one of its descendants. This switch limits the \n\
                  appearance of a symbol\'s tree to just once. \n\
    -h  this help message.\n";
}

lookup::lookup(int argc, char **argv)
{
	m_err.init(argc, argv);

	if ((m_errindex = process_args(argc, argv))) {
		m_err.print_cmd_errmsg(m_errindex, m_declstr, m_datafile);
		exit(m_errindex);
	}
}

int lookup::run()
{
	ifstream ifs(m_filelist.c_str());
	if(!ifs.is_open()) {
		cout << "Cannot open file: " << m_filelist << endl;
		exit(EXE_NOFILE);
	}

	while (getline(ifs, m_datafile)) {
		if ((m_errindex = execute(m_datafile))) {
			errpair ep = make_pair(m_errindex,
					       m_datafile);
			m_errors.push_back(ep);
		}
	}

	if ((m_flags & m_exemask) == KB_COUNT)
		cout << endl;

	for (auto it : m_errors)
		m_err.print_cmd_errmsg(it.first, m_declstr, it.second);

	return(m_errindex);
}

int lookup::count_bits(unsigned mask)
{
	int count = 0;

	do {
		count += mask & 1;
	} while (mask >>= 1);

	return count;
}

// Check for mutually exclusive flags.
bool lookup::check_flags()
{
	return !(count_bits(m_flags & m_exemask) > 1);
}

int lookup::process_args(int argc, char **argv)
{
	int argindex = 0;

	// skip over argv[0], which is the invocation of this app.
	++argv; --argc;

	if(!argc)
		return EXE_ARG2SML;

	m_flags = m_opts.get_options(&argindex, &argv[0], m_declstr, m_filelist);

	if (m_flags < 0 || !check_flags())
		return EXE_BADFORM;

	return EXE_OK;
}

int lookup::execute(string datafile)
{
	if(kb_read_cqnmap(datafile, m_cqnmap) != 0)
		return EXE_NOFILE;

	switch (m_flags & m_exemask) {
	case KB_COUNT   : return exe_count();
//	case KB_DECL    : return exe_decl();
	case KB_EXPORTS : return exe_exports();
	case KB_STRUCT  : return exe_struct();
	}
	return 0;
}

int lookup::exe_count()
{
	if (m_opts.kb_flags & KB_WHOLE_WORD) {
		m_crc = raw_crc32(m_declstr.c_str());
		pair<qniterator_t, qniterator_t> range;
		range = m_qnodes.equal_range(m_crc);
		m_count += distance(range.first, range.second);
	} else {
		for (auto it : m_qnodes) {
			qnode& qn = it.second;
			if (qn.sdecl.find(m_declstr) != string::npos)
				++m_count;
		}
	}

	cout << m_count << "\r";
	cout.flush();
	return m_count !=0 ? EXE_OK : EXE_NOTFOUND;
}

bool lookup::find_decl(qnode& qnr, string decl)
{
	m_crc = ::raw_crc32(decl.c_str());

	for (auto it : m_qnodes) {
		if (it.first == m_crc) {
			qnr = it.second;
			return true;
		}
	}
	return false;
}

int lookup::get_decl_list(std::vector<qnode> &retlist)
{
	for (auto it : m_qnodes) {
		if (it.first == m_crc)
			retlist.push_back(it.second);
	}
	DBG(cout << format("\"%s\" size: %d\n") % m_declstr %  m_declstr.size();)

	return retlist.size();
}

void lookup::fill_row(const qnode *qn, int level)
{
	row r;
	r.level = level;
	r.flags = qn->flags;
	r.decl = qn->sdecl;
	r.name = qn->sname;
	m_rows.push_back(r);
}

string &lookup::pad_out(int padsize)
{
	static string out;
	out.clear();
	while(padsize--) out += " ";
	return out;
}

void lookup::put_row(row &r)
{
	switch (r.level) {
	case LVL_FILE:
		cout << "FILE: " << r.decl << endl;
		break;
	case LVL_EXPORTED:
		cout << " EXPORTED: " << r.decl << " " << r.name << endl;
		break;
	case LVL_ARG:
		cout << ((r.flags & CTL_RETURN) ? "  RETURN: " : "  ARG: ");
		cout << r.decl << " " << r.name << endl;
		break;
	default:
		cout << pad_out(r.level) << r.decl << " " << r.name << endl;
		break;
	}
}

void lookup::put_rows()
{
	unsigned size = m_rows.size();
	for (unsigned i = 0; i < size; ++i) {
		row r = m_rows.back();
		put_row(r);
		m_rows.pop_back();
	}
}

int lookup::get_parents(qnode& qn)
{
	fill_row(&qn, qn.level);

	if (qn.level == 0)
		return EXE_OK;

	qnode& parent = *qn_lookup_parent(&qn, qn.parent.first);
	this->get_parents(parent);
	return EXE_OK;
}

int lookup::exe_struct()
{
	if (m_opts.kb_flags & KB_WHOLE_WORD) {
		unsigned long crc = raw_crc32(m_declstr.c_str());
		qnitpair_t range;
		range = m_qnodes.equal_range(crc);

		for_each (range.first, range.second,
			  [this](qnpair_t& lqp)
			  {
				qnode& qn = lqp.second;
				qn.crc = lqp.first;

				if (qn.flags & CTL_BACKPTR)
				//|| !(qn.flags & CTL_HASLIST))
					return;

				m_rows.clear();
				m_rows.reserve(qn.level);
				this->get_parents(qn);
				put_rows();
			  });
	} else {
		for (auto it : m_qnodes) {
			qnode& qn = it.second;
			qn.crc = it.first;

			if (qn.sdecl.find(m_declstr) != string::npos) {
				m_rows.clear();
				m_rows.reserve(qn.level);
				this->get_parents(qn);
			}
			put_rows();
		}
	}

	return EXE_OK;
}

int lookup::get_children(qnode& qn)
{

	return EXE_OK;
}

int lookup::exe_exports()
{
	for (auto it : m_qnodes) {
		qnode& qn = it.second;
		qn.crc = it.first;

		if (qn.sdecl.find(m_declstr) != string::npos) {
			m_rows.clear();
			m_rows.reserve(qn.level);
		}
	}
	return EXE_OK;
}


/************************************************
** main()
************************************************/
int main(int argc, char *argv[])
{
	lookup lu(argc, argv);
	return lu.run();
}
