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

#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/algorithm/string.hpp>

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

string lookup::get_version()
{
	return "\
\n\
kabi-lookup Version 3.6.2\n\
\n";
}

string lookup::get_helptext()
{
	return "\
kabi-lookup [-vwl] -e|s|c|d symbol [-f file-list] [-m mask] [-p path] \n\
    Searches a kabi database for symbols. The results of the search \n\
    are printed to stdout and indented hierarchically.\n\
\n\
    Switches e,s,c,d are required, but mutually exlusive. \n\
    Only one can be selected. \n\
\n\
    Switches v,w,l,m,p, and f are optional. \n\
    Switch l must be used with switch w, or the program will exit with\n\
    a message. All may be used concurrently.\n\
\n\
    -e symbol   - Find the EXPORTED function defined by symbol. Print the \n\
                  function and its argument list, and with the -v switch, \n\
                  the descendants any of its nonscalar arguments or returns. \n\
                  The -v switch generates a lot of output, but is useful if \n\
                  you need to see everything the function depends upon.\n\
    -s symbol   - Search for the symbol and print every exported function \n\
                  that depends on the symbol. The hierachical position of \n\
                  the symbol is displayed as indented by the level at which\n\
                  it was discovered. \n\
                  With the -w switch, the string must contain the compound \n\
                  type enclosed in quotes, e.g.\'struct foo\', \'union bar\'\n\
                  \'enum int foo_states\' \n\
                  With the -v switch, the symbol will be printed everywhere \n\
                  it exists as well as its hierarchical ancestors, indented \n\
                  according to the hierarchical level at which they were \n\
                  discovered. This generates a lot of output.\n\
    -c symbol   - Counts the instances of the symbol in the kernel tree. \n\
    -d symbol   - Seeks a data structure and prints its members to stdout. \n\
                  With the -v switch, descendants of nonscalar members will \n\
                  also be printed.\n\
    -l          - White listed symbols only. Limits search to symbols in the\n\
                  kabi white list, if it exists.\n\
    -m mask     - Limits the search to directories and files containing the\n\
                  mask string. \n\
    -1          - Return only the first instance discovered.\n\
    -p          - Path to top of kernel tree, if operating in a different\n\
                  directory.\n\
    -v          - Verbose output. Default is quiet.\n\
    -w          - Whole word search, default is substring match. \n\
    -f filelist - Optional path to list of data files created by kabi-parser\n\
                  during the kernel build, or using the kabi-data.sh script.\n\
                  The default path is redhat/kabi/kabi-datafiles.list \n\
                  relative to the top of the kernel tree.\n\
    -V          - Print version number.\n\
    -h          - this help message.\n";
}

/************************************************
** main()
************************************************/
int main(int argc, char *argv[])
{
	lookup lu(argc, argv);
	return lu.run();
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
 * lookup::process_args(int argc, char **argv)
 */
int lookup::process_args(int argc, char **argv)
{
	int argindex = 0;

	// skip over argv[0], which is the invocation of this app.
	++argv; --argc;

	if(!argc)
		return EXE_ARG2SML;

	m_flags |= m_opts.get_options(&argindex, &argv[0], m_declstr,
				      m_filelist, m_maskstr, m_userdir);

	if (m_flags & KB_VERBOSE)
		m_flags &= ~KB_QUIET;

	if (m_flags < 0 || !check_flags())
		return EXE_BADFORM;

	return EXE_OK;
}

/*****************************************************************************
 * lookup::check_flags() - Check for mutually exclusive flags.
 */
bool lookup::check_flags()
{
	if ((m_flags & KB_VERBOSE) && (m_flags & KB_QUIET))
		return false;

	if ((m_flags & KB_WHITE_LIST) && !(m_flags & KB_WHOLE_WORD))
		return false;

	return (count_bits(m_flags & m_exemask) == 1);
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
 */
int lookup::run()
{
	ifstream ifs;

	if (set_working_directory())
		goto lookup_error;

	m_filelist = m_kabidir + m_filelist;
	ifs.open(m_filelist);

	if (!ifs.is_open())
		report_nopath(m_filelist.c_str(), "file");

	if (m_flags & KB_WHITE_LIST) {
		if (!build_whitelist())
			goto lookup_error;

		if (!check_whitelist()) {
			m_errvec.push_back(m_declstr);
			goto lookup_error;
		}
	}

	while (getline(ifs, m_datafile)) {

		if ((m_flags & KB_MASKSTR) &&
		    (m_datafile.find(m_maskstr) == string::npos))
			continue;

		if (!(m_flags & KB_COUNT)) {
			cerr << "\33[2K\r";	// return to start of line
			cerr << m_datafile;
			//cerr.flush();
		}

		m_errindex = execute(m_datafile);

		// If we're looking for EXPORTed symbols or struct decls,
		// then no need to look any further once we find one.
		//
		if (m_isfound && (m_flags & KB_WHOLE_WORD)
			      && ((m_flags & KB_EXPORTS)
			      ||  (m_flags & KB_DECL)))
			break;

		// Break also if the KB_JUSTONE flag is set.
		//
		if (m_isfound && (m_flags & KB_JUSTONE))
			break;
	}

	if (m_flags & KB_COUNT)
		cerr << "\33[2K\r" << m_count;

	if (!(m_flags & KB_COUNT))
		cerr << "\33[2K\r";

	//cerr.flush();
	cout << endl;
	ifs.close();

	if (m_isfound)
		m_errindex = EXE_OK;

	m_errvec.push_back(m_declstr);
lookup_error:
	m_err.print_errmsg(m_errindex, m_errvec);

	if (set_start_directory())
		m_err.print_errmsg(m_errindex, m_errvec);

	return(m_errindex);
}

/*****************************************************************************
 * void assure_trailing_slash()
 *
 * If the directory specification does not have a trailing slash, give it
 * one.
 */
void inline lookup::assure_trailing_slash(string& dirspec)
{
	if (dirspec.back() != '/')
		dirspec += '/';
}

/*****************************************************************************
 * int set_working_directory()
 *
 * Saves the current working directory. If the user entered a different
 * directory using the -p option, then change to thad directory. At the
 * end of processing, return to the start directory.
 *
 */
int lookup::set_working_directory()
{
	if (m_userdir.empty())
		return (m_errindex = EXE_OK);

	char *pwd = get_current_dir_name();

	m_startdir.assign(pwd);
	free(pwd);
	assure_trailing_slash(m_startdir);
	assure_trailing_slash(m_userdir);

	if (m_userdir == m_startdir)
		return (m_errindex = EXE_OK);

	if (chdir(m_userdir.c_str())) {
		m_errvec.push_back(m_userdir);
		return (m_errindex =  EXE_NODIR);
	}

	return (m_errindex = EXE_OK);
}

/*****************************************************************************
 * int set_start_directory()
 *
 * Returns to the directory where we invoked the program.
 *
 */
int lookup::set_start_directory()
{
	if (m_userdir == m_startdir)
		return (m_errindex = EXE_OK);

	if (chdir(m_startdir.c_str())) {
		m_errvec.push_back(m_startdir);
		return (m_errindex = EXE_NODIR);
	}

	return (m_errindex = EXE_OK);
}

/*****************************************************************************
 * lookup::check_whitelist()
 *
 * Check to see if the m_declstr is in the whitelist vector built by
 * build_whitelist()
 *
 */
bool lookup::check_whitelist()
{
	string decl;
	bool found = false;

	if (m_flags & KB_WHOLE_WORD) {
		vector<string> toklist;

		boost::split(toklist, m_declstr, boost::is_any_of(" "));

		if (toklist.size() > 1 )
			decl = toklist[1];
		else
			decl = m_declstr;
	}

	for (auto it : m_whitelist) {
		if (decl == it) {
			found = true;
			break;
		}
	}

	m_errindex = found ? EXE_OK : EXE_NOTWHITE;
	return found;
}

/*****************************************************************************
 * lookup::build_whitelist()
 *
 * Initialize the m_whitelist vector member with the symbols in the kabi
 * whitelists.
 *
 */
bool lookup::build_whitelist() {

	struct dirent *ent;
	bool found = false;

	if ((m_kbdir = opendir(m_kabidir.c_str())) == NULL)
		report_nopath(m_kabidir.c_str(), "directory");

	while ((ent = readdir(m_kbdir)) != NULL) {
		string line;
		string path = m_kabidir + string(ent->d_name);
		ifstream ifs(path);

		if ((!ifs.is_open()) || (!strstr(ent->d_name, "Module.kabi")))
			continue;

		found = true;

		while (getline(ifs, line)) {
			boost::char_separator<char> sep(" \t");
			tokenizer<boost::char_separator<char>> tok(line, sep);
			int index = 0;

			// Second token in the line is the whitelisted symbol
			BOOST_FOREACH(string str, tok) {
				if (++index == 2) {
					m_whitelist.push_back(str);
					break;
				}
			}
		}
		ifs.close();
	}
	closedir(m_kbdir);
	m_errindex = found ? EXE_OK : EXE_NO_WLIST;
	return found;
}

/*****************************************************************************
 * lookup::report_nopath(const char *name, const char *path)
 */
void lookup::report_nopath(const char* name, const char* path)
{
	cout << "\nCannot open " << path << ": " << name << endl;
	exit(EXE_NOFILE);
}

/*****************************************************************************
 * lookup::execute(string datafile)
 */
int lookup::execute(string datafile)
{
	if(kb_read_dnodemap(datafile, m_dnmap) != 0)
		return EXE_NOFILE;

	switch (m_flags & m_exemask) {
	case KB_STRUCT  : return exe_struct();
	case KB_EXPORTS : return exe_exports();
	case KB_DECL    : return exe_decl();
	case KB_COUNT   : return exe_count();
	}
	return 0;
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

/*****************************************************************************
 * lookup::exe_count() - count the appearances of the symbol in provided scope
 *
 * Scope can be limited using the -m switch to limit the search to directories
 * and files containing the mask string passed with the -m option.
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
	//cerr.flush();
	return m_count !=0 ? EXE_OK : EXE_NOTFOUND;
}

/*****************************************************************************
 * lookup::is_whitelisted(string &ksym)
 *
 * Search the m_whitelist vector for a matching symbol.
 */
bool lookup::is_whitelisted(string& ksym)
{
	bool found;
	vector<string>::iterator vit;

	vit = find_if(m_whitelist.begin(), m_whitelist.end(),
		[ksym](string& str) {
			return (str == ksym);
		});

	found = vit != m_whitelist.end();
	return found;
}

/*****************************************************************************
 * lookup::is_function_whitelisted(cnode &cn)
 *
 * Find the function at the top of the hierarchy where this cnode was found
 * and search the whitelist for a match.
 *
 */
bool lookup::is_function_whitelisted(cnode& cn)
{
	dnode* func = kb_lookup_dnode(cn.function);
	if (!func) return false;
	cnode& fcn = func->siblings.begin()->second;
	return is_whitelisted(fcn.name);
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
