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
#include <iostream>
#include <boost/format.hpp>
#include <boost/range/adaptor/reversed.hpp>

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
kabi-lookup -[qw] -c|d|e|s symbol [-f file-list]\n\
    Searches a kabi database for symbols. The results of the search \n\
    are printed to stdout and indented hierarchically.\n\
\n\
    -c symbol   - Counts the instances of the symbol in the kabi tree. \n\
    -s symbol   - Prints to stdout every exported function that is implicitly \n\
                  or explicitly affected by the symbol. In default mode, the \n\
                  chain from the exported function to the symbol is printed.\n\
                  It is advisable to use \"-c symbol\" first. \n\
                  The -q switch can be used to suppress output between the \n\
                  symbol and its ancestral function or agrument.\n\
    -e symbol   - Specific to EXPORTED functions. Prints the function, \n\
                  and its argument list as well as all the descendants of \n\
                  any of its nonscalar arguments. \n\
    -d symbol   - Seeks a data structure and prints its members to stdout. \n\
                  Without the -q switch, descendants of nonscalar members \n\
                  will also be printed.\n\
    -q          - \"Quiet\" option limits the amount of output. \n\
                  Default is verbose.\n\
    -w          - whole words only, default is \"match any and all\" \n\
    -f filelist - Optional list of data files that were created by kabi-parser. \n\
                  The default list created by running kabi-data.sh is \n\
		  \"./redhat/kabi/parser/kabi-files.list\" relative to the \n\
                  top of the kernel tree. \n\
    -u subdir   - Limit the search to a specific kernel subdirectory. \n\
    -h          - this help message.\n";
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

		if ((m_flags & KB_SUBDIR) &&
		    (m_datafile.find(m_subdir) == string::npos))
			continue;

		if (!(m_flags & KB_COUNT)) {
			cout << m_datafile << "\r";
			cout.flush();
		}

		m_errindex = execute(m_datafile);

		if (!(m_flags & KB_COUNT)) {
			cout << "\33[2K\r";
			cout.flush();
		}

		if (m_isfound && (m_flags & KB_WHOLE_WORD)
			      && ((m_flags & KB_EXPORTS)
			      ||  (m_flags & KB_DECL)))
			break;
	}

	cout << endl;

	if (m_isfound)
		m_errindex = EXE_OK;

	m_err.print_cmd_errmsg(m_errindex, m_declstr, m_filelist);
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
	if (m_flags & KB_QUIET)
		m_flags &= ~KB_VERBOSE;

	return !(count_bits(m_flags & m_exemask) > 1);
}

int lookup::process_args(int argc, char **argv)
{
	int argindex = 0;

	// skip over argv[0], which is the invocation of this app.
	++argv; --argc;

	if(!argc)
		return EXE_ARG2SML;

	m_flags = KB_VERBOSE;
	m_flags |= m_opts.get_options(&argindex, &argv[0],
					m_declstr, m_filelist, m_subdir);

	if (m_flags < 0 || !check_flags())
		return EXE_BADFORM;

	return EXE_OK;
}

int lookup::execute(string datafile)
{
	if(kb_read_dnodemap(datafile, m_dnmap) != 0)
		return EXE_NOFILE;

	switch (m_flags & m_exemask) {
	case KB_COUNT   : return exe_count();
	case KB_DECL    : return exe_decl();
	case KB_EXPORTS : return exe_exports();
	case KB_STRUCT  : return exe_struct();
	}
	return 0;
}

int lookup::exe_count()
{
	if (m_opts.kb_flags & KB_WHOLE_WORD) {
		m_crc = raw_crc32(m_declstr.c_str());
		dnode* dn = kb_lookup_dnode(m_crc);
		m_count += dn ? dn->siblings.size() : 0;
	} else {
		for (auto it : m_dnmap) {
			dnode& dn = it.second;
			if (dn.decl.find(m_declstr) != string::npos)
				++m_count;
		}
	}

	cout << m_count << "\r";
	cout.flush();
	return m_count !=0 ? EXE_OK : EXE_NOTFOUND;
}

bool lookup::find_decl(dnode &dnr, string decl)
{
	m_crc = ::raw_crc32(decl.c_str());
	dnode* dn = kb_lookup_dnode(m_crc);

	if (dn) {
		dnr = *dn;
		return true;
	}
	return false;
}

/*****************************************************************************
 * lookup::get_parents(cnode &cn)
 *
 * Lookup the parent's dnode using the crc from the parent field of the
 * cnode passed as an arg.
 *
 * Traverse the parent's siblings map looking for the first one having
 * the same ancestry as the cnode that was passed as an argument and is
 * one level up from the cnode passed as an argument.
 *
 * Do this recursively, until we either run out of siblings or we've
 * reached the top of the hierarchy, so that the crc is zero.
 *
 */
int lookup::get_parents(cnode& cn)
{
	crc_t crc = cn.parent.second;

	if (!crc)
		return EXE_OK;

	dnode parentdn = *kb_lookup_dnode(crc);
	cnodemap siblings = parentdn.siblings;
	cniterator cnit;

	cnit = find_if(siblings.begin(), siblings.end(),
		[cn](cnpair& lcnp) {
			cnode lcn = lcnp.second;
			int nextlevelup = cn.level - 1;

			switch (cn.level) {
			case LVL_FILE :
				return true;
			case LVL_EXPORTED :
				return (lcn.level == nextlevelup);
			case LVL_ARG  :
				return ((lcn.level == nextlevelup) &&
					(lcn.function == cn.function));
			default :
				return ((lcn.level == nextlevelup) &&
					(lcn.argument == cn.argument) &&
					(lcn.function == cn.function));
			}
		});

	if (cnit == siblings.end())
		return EXE_OK;

	cnode parentcn = (*cnit).second;
	m_rowman.fill_row(parentdn, parentcn);
	this->get_parents(parentcn);
	return EXE_OK;
}

/*****************************************************************************
 * lookup::get_siblings(dnode& dn)
 * dn - reference to a dnode
 *
 * Walk the siblings cnodemap in the dnode to access each instance of the
 * symbol characterized by the dnode.
 *
 * First we need to divide the list by ancestry.
 * Then we order each of the unique ancestries by level, which is the depth
 * at which a given instance of a symbol (dnode) was instantiated.
 *
 */
int lookup::get_siblings_up(dnode& dn)
{
	for (auto it : dn.siblings) {
		cnode cn = it.second;
		m_rowman.fill_row(dn, cn);
		get_parents(cn);
	}
	return EXE_OK;
}

/*****************************************************************************
 * lookup::get_children(dnode& dn)
 *
 * Given references to a dnode and a cnode instance of it, walk the dnode's
 * children crcmap and gather the info on the children.
 * This is done reursively, until we've parsed all the children and all
 * their descendants.
 */
int lookup::get_children(dnode& dn)
{
	for (auto i : dn.children) {
		int order = i.first;
		crc_t crc = i.second;
		dnode dn = *kb_lookup_dnode(crc);
		cnodemap siblings = dn.siblings;
		cnode cn = siblings[order];

		m_rowman.fill_row(dn, cn);

		if (cn.flags & CTL_BACKPTR)
			continue;

		get_children(dn);
	}
	return EXE_OK;
}

/*****************************************************************************
 * lookup::get_siblings(dnode& dn)
 * dn - reference to a dnode
 *
 * Walk the siblings cnodemap in the dnode to access each instance of the
 * symbol characterized by the dnode.
 */
int lookup::get_siblings(dnode& dn)
{
	for (auto it : dn.siblings) {
		cnode cn = it.second;
		m_rowman.fill_row(dn, cn);
		get_children(dn);
	}
	return EXE_OK;
}

/*****************************************************************************
 * lookup::exe_struct()
 *
 * Search the graph for a struct matching the input string in m_declstr and
 * dump its hierarchy everywhere it's encountered all the way up to the
 * file level.
 */
int lookup::exe_struct()
{
	bool quiet = m_flags & KB_QUIET;

	if (m_opts.kb_flags & KB_WHOLE_WORD) {
		unsigned long crc = raw_crc32(m_declstr.c_str());
		dnode* dn = kb_lookup_dnode(crc);

		if (!dn)
			return EXE_NOTFOUND_SIMPLE;

		m_isfound = true;
		m_rowman.rows.clear();
		get_siblings_up(*dn);
		m_rowman.put_rows_from_back(quiet);

	} else {

		for (auto it : m_dnmap) {
			dnode& dn = it.second;

			if (dn.decl.find(m_declstr) == string::npos)
				continue;

			m_isfound = true;
			m_rowman.rows.clear();
			get_siblings_up(dn);
			m_rowman.put_rows_from_back(quiet);
		}
	}

	return m_isfound ? EXE_OK : EXE_NOTFOUND_SIMPLE;
}

/*****************************************************************************
 * int lookup::exe_exports()
 *
 * Search the graph for exported symbols. If the whole word flag is set, then
 * the search will look for an exact match. In that case, it will find at most
 * one matching exported symbol.
 * If not, the code will search the graph for the string where ever it appears,
 * even as a substring, but only exported symbols are considered.
 */
int lookup::exe_exports()
{
	bool quiet = m_flags & KB_QUIET;

	if (m_opts.kb_flags & KB_WHOLE_WORD) {
		unsigned long crc = raw_crc32(m_declstr.c_str());
		dnode* dn = kb_lookup_dnode(crc);

		if (!dn)
			return EXE_NOTFOUND_SIMPLE;

		// If there is not exactly one sibling, then this is not an
		// exported symbol. Exports all exist in the same name space
		// and must be unique; there can only be one.
		if (dn->siblings.size() != 1)
			return EXE_NOTFOUND_SIMPLE;

		m_isfound = true;
		m_rowman.rows.clear();
		get_siblings(*dn);
		m_rowman.put_rows_from_front(quiet);

	} else {

		for (auto it : m_dnmap) {
			dnode& dn = it.second;

			if (dn.siblings.size() != 1)
				continue;

			cniterator cnit = dn.siblings.begin();
			cnode cn = cnit->second;

			if (!(cn.flags & CTL_EXPORTED))
				continue;

			if (cn.name.find(m_declstr) == string::npos)
				continue;

			m_isfound = true;
			m_rowman.rows.clear();
			get_siblings(dn);
			m_rowman.put_rows_from_front(quiet);
		}
	}

	return m_isfound ? EXE_OK : EXE_NOTFOUND_SIMPLE;
}

int lookup::exe_decl()
{
	bool quiet = m_flags & KB_QUIET;

	if (m_opts.kb_flags & KB_WHOLE_WORD) {
		unsigned long crc = raw_crc32(m_declstr.c_str());
		dnode* dn = kb_lookup_dnode(crc);

		if (!dn)
			return EXE_NOTFOUND_SIMPLE;

		cniterator cnit = dn->siblings.begin();
		cnode cn = cnit->second;
		m_isfound = true;
		m_rowman.rows.clear();
		m_rowman.fill_row(*dn, cn);

		get_children(*dn);
		m_rowman.put_rows_from_front_normalized(quiet);

	} else {

		for (auto it : m_dnmap) {
			dnode& dn = it.second;
			cniterator cnit = dn.siblings.begin();
			cnode cn = cnit->second;

			if (dn.decl.find(m_declstr) == string::npos)
				continue;

			m_isfound = true;
			m_rowman.rows.clear();
			m_rowman.fill_row(dn, cn);

			get_children(dn);
			m_rowman.put_rows_from_front_normalized(quiet);
		}
	}

	return m_isfound ? EXE_OK : EXE_NOTFOUND_SIMPLE;
}

/************************************************
** main()
************************************************/
int main(int argc, char *argv[])
{
	lookup lu(argc, argv);
	return lu.run();
}
