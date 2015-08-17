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
#include <dirent.h>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "checksum.h"
#include "kabilookup.h"

using namespace std;
using boost::format;
using boost::tokenizer;

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
kabi-lookup [-q|w] -e|s|c|d symbol [-f file-list] [-m mask] \n\
    Searches a kabi database for symbols. The results of the search \n\
    are printed to stdout and indented hierarchically.\n\
\n\
    Switches e,s,c,d are required, but mutually exlusive. \n\
    Only one can be selected. \n\
\n\
    Switches q,w,l,m,p, and f are optional and do not conflict.\n\
    They may be used concurrently.\n\
\n\
    -e symbol   - Find the EXPORTED function defined by symbol. Print the \n\
                  function and its argument list as well descendants of \n\
                  any of its nonscalar arguments or returns. \n\
    -s symbol   - Search for the symbol and print every exported function \n\
                  that depends on the symbol. The hierachical position of \n\
                  the symbol is displayed as indented by the level at which\n\
                  it was discovered. \n\
                  With the -w switch, the string must contain the compound \n\
                  type enclosed in quotes, e.g.\'struct foo\', \'union bar\'\n\
                  \'enum int foo_states\' \n\
    -c symbol   - Counts the instances of the symbol in the kernel tree. \n\
    -d symbol   - Seeks a data structure and prints its members to stdout. \n\
                  Without the -q switch, descendants of nonscalar members \n\
                  will also be printed.\n\
    -l          - White listed symbols only. Limits search to symbols in the\n\
                  kabi white list, if it exists.\n\
                  Whole word searches only (-w). \n\
    -m mask     - Limits the search to directories and files containing the\n\
                  mask string. \n\
    -q          - \"Quiet\" option limits the amount of output. \n\
                  More \'q\', more quiet, e.g. -qq\n\
                  Default is verbose.\n\
    -p          - Path to top of kernel tree, if operating in a different\n\
                  directory.\n\
    -w          - Whole word search, default is \"match any and all\" \n\
    -f filelist - List of data files that were created by kabi-parser.\n\
                  The default list created by running kabi-data.sh is \n\
                  \"./redhat/kabi/kabi-datafiles.list\" relative to the \n\
                  top of the kernel tree. \n\
    -h          - this help message.\n";
}

/*****************************************************************************
 * lookup::lookup - constructor
 *
 */
lookup::lookup(int argc, char **argv)
{
	m_err.init(argc, argv);

	if ((m_errindex = process_args(argc, argv))) {
		m_errvec.push_back(m_declstr);
		m_errvec.push_back(m_datafile);
		m_err.print_errmsg(m_errindex, m_errvec);
		exit(m_errindex);
	}
}

/*****************************************************************************
 */
void lookup::report_nopath(const char* name, const char* path)
{
	cout << "\nCannot open " << path << ": " << name << endl;
	exit(EXE_NOFILE);
}

/*****************************************************************************
 */
bool lookup::is_ksym_in_line(string& line, string& ksym)
{
	boost::char_separator<char> sep(" \t");
	tokenizer<boost::char_separator<char>> tok(line, sep);

	BOOST_FOREACH(string str, tok)
		if (str == ksym)
			return true;

	return false;
}

/*****************************************************************************
 */
bool lookup::is_whitelisted(string& ksym)
{
	DIR *dir;
	struct dirent *ent;
	bool found = false;

	if (m_pathstr.back() != '/') m_pathstr += "/";
	string dirname = m_pathstr + m_kabidir;

	if ((dir = opendir(dirname.c_str())) == NULL)
		report_nopath(dirname.c_str(), "directory");

	while (!found && (ent = readdir(dir)) != NULL) {

		string line;
		string path = dirname + string(ent->d_name);
		ifstream ifs(path);

		if (!ifs.is_open())
			continue;

		if (!strstr(ent->d_name, "Module.kabi"))
			continue;

		while (getline(ifs, line))
			if ((found = is_ksym_in_line(line, ksym)))
				break;	}
	return found;
}


/*****************************************************************************
 */
int lookup::run()
{
	ifstream ifs(m_filelist.c_str());

	if(!ifs.is_open())
		report_nopath(m_filelist.c_str(), "file");

	while (getline(ifs, m_datafile)) {

		if ((m_flags & KB_MASKSTR) &&
		    (m_datafile.find(m_maskstr) == string::npos))
			continue;

		if (!(m_flags & KB_COUNT)) {
			cerr << "\33[2K\r";
			cerr << m_datafile;
			cerr.flush();
		}

		m_errindex = execute(m_datafile);

		if (m_isfound && (m_flags & KB_WHOLE_WORD)
			      && ((m_flags & KB_EXPORTS)
			      ||  (m_flags & KB_DECL)))
			break;
	}

	if (m_flags & KB_COUNT)
		cout << "\33[2K\r" << m_count;

	if (!(m_flags & KB_COUNT))
		cerr << "\33[2K\r";

	cerr.flush();
	cout << endl;
	ifs.close();

	if (m_isfound)
		m_errindex = EXE_OK;

	m_errvec.push_back(m_declstr);
	m_err.print_errmsg(m_errindex, m_errvec);
	return(m_errindex);
}

/*****************************************************************************
 * lookup::count_bits(unsigned mask) - count number of bits set in the mask
 */
int lookup::count_bits(unsigned mask)
{
	int count = 0;

	do {
		count += mask & 1;
	} while (mask >>= 1);

	return count;
}

/*****************************************************************************
 * lookup::check_flags() - Check for mutually exclusive flags.
 */
bool lookup::check_flags()
{
	if (m_flags & KB_QUIET)
		m_flags &= ~KB_VERBOSE;

	if ((m_flags & KB_WHITE_LIST) && !(m_flags & KB_WHOLE_WORD))
		return false;

	return (count_bits(m_flags & m_exemask) == 1);
}

/*****************************************************************************
 * lookup::process_args(int argc, char **argv)
 */
int lookup::process_args(int argc, char **argv)
{
	int argindex = 0;

	// skip over argv[0], which is the invocation of this app.
	++argv; --argc;

	if(!argc)
		return EXE_ARG2SML;

	m_flags = KB_VERBOSE;
	m_flags |= m_opts.get_options(&argindex, &argv[0], m_declstr,
				      m_filelist, m_maskstr, m_pathstr);

	if (m_flags < 0 || !check_flags())
		return EXE_BADFORM;

	return EXE_OK;
}

/*****************************************************************************
 * lookup::execute(string datafile)
 */
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

/*****************************************************************************
 * lookup::exe_count() - count the appearances of the symbol in provided scope
 *
 * Scope can be limited using the -u switch to limit the search to directories
 * and files containing the mask string passed with the -u option.
 *
 */
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

	cerr << "\33[2K\r";
	cerr << m_count;
	cerr.flush();
	return m_count !=0 ? EXE_OK : EXE_NOTFOUND;
}

/*****************************************************************************
 *
 *
 *
 */
bool lookup::is_function_whitelisted(cnode& cn)
{
	dnode* func = kb_lookup_dnode(cn.function);
	if (!func) return false;
	return is_whitelisted(func->decl);
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
			cnode ccn = cn;
			return kb_is_adjacent(ccn, lcn, SK_PARENT);
		});

	if (cnit == siblings.end())
		return EXE_OK;

	cnode parentcn = cnit->second;
	m_rowman.fill_row(parentdn, parentcn);
	this->get_parents(parentcn);
	return EXE_OK;
}

/*****************************************************************************
 * lookup::get_siblings(dnode& dn)
 * dn - reference to a dnode
 *
 * Walk the siblings cnodemap in the dnode to access each instance of the
 * symbol characterized by the dnode. If we're only looking for whitelisted
 * symbols, and if the topmost symbol in the ancestry (function) is not
 * whitelisted, then skip it.
 *
 */
int lookup::get_siblings_up(dnode& dn)
{
	for (auto it : dn.siblings) {
		cnode cn = it.second;

		if ((m_flags & KB_WHITE_LIST) &&
		   !(is_function_whitelisted(cn)))
			continue;

		m_isfound = true;
		m_rowman.fill_row(dn, cn);

		DBG(m_rowman.print_row(m_rowman.rows.back());)
		get_parents(cn);
	}
	return EXE_OK;
}


/*****************************************************************************
 * lookup::is_dup(crc_t crc)
 *
 * Check the m_dups vector for this crc. Return true if we've seen it before.
 */
bool lookup::is_dup(crc_t crc)
{
	vector<crc_t>::iterator it;

	it = find_if (m_dups.begin(), m_dups.end(),
		[crc](crc_t lcrc)
		{
			return lcrc == crc;
		});

	return (it == m_dups.end()) ? false : true;
}

/*****************************************************************************
 * lookup::get_children(dnode& pdn, cnode& pcn)
 *
 * pdn - parent dnode
 * pcn - parent cnode
 *
 * Given a reference to a dnode, walk the dnode's children crcmap and gather
 * the info on the children. This is done reursively, until we've parsed all
 * the children and all their descendants.
 */
int lookup::get_children(dnode& pdn, cnode& pcn)
{
	for (auto i : pdn.children) {
		int order = i.first;
		crc_t crc = i.second;
		dnode cdn = *kb_lookup_dnode(crc);	// child dnode
		cnodemap siblings = cdn.siblings;
		cnode ccn = siblings[order];

		// Backpointers and dups are "virtualized", that is, there
		// is only one cnode for all. In those cases, the level
		// field is only correct for the first one encountered.
		// To assure that we have the correct level, simply set
		// it to parent cnode level + 1.
		ccn.level = pcn.level + 1;

		if (ccn.level <= LVL_ARG)
			m_dups.clear();

		m_rowman.fill_row(cdn, ccn);
		DBG(m_rowman.print_row(m_rowman.rows.back());)

		if ((is_dup(crc)) || (ccn.flags & CTL_BACKPTR))
			continue;

		m_dups.push_back(crc);
		get_children(cdn, ccn);
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
		DBG(m_rowman.print_row(m_rowman.rows.back());)
		get_children(dn, cn);
	}
	return EXE_OK;
}

/*****************************************************************************
 * lookup::get_siblings_exported(dnode& dn)
 * dn - reference to a dnode
 *
 * Walk the siblings cnodemap in the dnode to access each instance of the
 * symbol characterized by the dnode.
 */
int lookup::get_siblings_exported(dnode& dn)
{
	bool found = false;
	for (auto it : dn.siblings) {
		cnode cn = it.second;

		if (!(cn.flags & CTL_EXPORTED))
			continue;

		m_rowman.fill_row(dn, cn);
		DBG(m_rowman.print_row(m_rowman.rows.back());)
		get_children(dn, cn);
		found = true;
	}
	return found ? EXE_OK : EXE_NOTFOUND;
}

/*****************************************************************************
 * lookup::get_file_of_export(dnode &dn)
 *
 * Gets the name of the file that has the exported function characterized
 * by the dnode argument.
 *
 */
int lookup::get_file_of_export(dnode &dn)
{
	auto it = dn.siblings.cbegin();
	cnode cn = it->second;
	crc_t crc = cn.parent.second;

	if (!crc)
		return EXE_NOTFOUND;

	dnode parentdn = *kb_lookup_dnode(crc);
	it = parentdn.siblings.begin();
	cnode parentcn = it->second;
	m_rowman.fill_row(parentdn, parentcn);
	DBG(m_rowman.print_row(m_rowman.rows.back());)

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
			return EXE_NOTFOUND;

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

	return m_isfound ? EXE_OK : EXE_NOTFOUND;
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
	int status = EXE_OK;

	if (m_opts.kb_flags & KB_WHOLE_WORD) {
		unsigned long crc = raw_crc32(m_declstr.c_str());
		dnode* dn = kb_lookup_dnode(crc);

		if (!dn)
			return EXE_NOTFOUND;

		if ((m_flags & KB_WHITE_LIST) && !(is_whitelisted(m_declstr)))
			return EXE_NOTWHITE;

		m_isfound = true;
		m_rowman.rows.clear();
		get_file_of_export(*dn);
		status = get_siblings_exported(*dn);

		if (status == EXE_OK)
			m_rowman.put_rows_from_front(quiet);

	} else {

		for (auto it : m_dnmap) {
			dnode& dn = it.second;

			cniterator cnit = dn.siblings.begin();
			cnode cn = cnit->second;

			if ((cn.level != LVL_EXPORTED) ||
			    (cn.name.find(m_declstr) == string::npos))
				continue;

			m_isfound = true;
			m_rowman.rows.clear();
			get_file_of_export(dn);
			status = get_siblings_exported(dn);

			if (status == EXE_OK)
				m_rowman.put_rows_from_front(quiet);
		}
	}

	return m_isfound ? EXE_OK : EXE_NOTFOUND;
}

/*****************************************************************************
 * lookout::exe_decl()
 *
 * Search for the first instance of the data structure characterized by the
 * declaration passed to the program and stored in m_declstr. All the
 * members of the data structure are printed to the screen, and the
 * program exits.
 *
 */
int lookup::exe_decl()
{
	bool quiet = m_flags & KB_QUIET;

	if (m_opts.kb_flags & KB_WHOLE_WORD) {
		unsigned long crc = raw_crc32(m_declstr.c_str());
		dnode* dn = kb_lookup_dnode(crc);

		if (!dn)
			return EXE_NOTFOUND;

		cniterator cnit = dn->siblings.begin();
		cnode cn = cnit->second;
		m_isfound = true;
		m_rowman.rows.clear();
		m_rowman.fill_row(*dn, cn);

		get_children(*dn, cn);
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

			get_children(dn, cn);
			m_rowman.put_rows_from_front_normalized(quiet);
		}
	}

	return m_isfound ? EXE_OK : EXE_NOTFOUND;
}

/************************************************
** main()
************************************************/
int main(int argc, char *argv[])
{
	lookup lu(argc, argv);
	return lu.run();
}
