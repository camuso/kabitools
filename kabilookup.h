/* kabi-lookup.h
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

#ifndef KABILOOKUP_H
#define KABILOOKUP_H

#include <map>
#include <vector>
#include <dirent.h>
#include "kabi-map.h"
#include "options.h"
#include "error.h"
#include "rowman.h"

class lookup
{
public:
	lookup(){}
	lookup(int argc, char **argv);
	int run();
	static std::string get_helptext();
	static std::string get_version();
private:
	int process_args(int argc, char **argv);
	bool check_flags();
	int count_bits(unsigned mask);
	int get_parents(cnode &cn);
	int get_children(dnode& pdn, cnode &pcn);
	int get_siblings(dnode& dn);
	int get_siblings_up(dnode& dn);
	int get_siblings_exported(dnode& dn);
	int execute(std::string datafile);
	int exe_count();
	int exe_struct();
	int exe_exports();
	int exe_decl();
	int get_file_of_export(dnode& dn);
	int set_working_directory();
	int set_start_directory()	;
	void report_nopath(const char *name, const char *path);
	void assure_trailing_slash(std::string& dirspec);
	bool is_dup(crc_t crc);
	bool is_whitelisted(std::string& ksym);
	bool is_function_whitelisted(cnode& cn);
	bool build_whitelist();

	// member classes
	dnodemap& m_dnmap = kb_get_public_dnodemap();
	rowman m_rowman;
	options m_opts;
	error m_err;

	// member basetypes
	typedef std::pair<int, std::string> errpair;

	//std::vector<errpair> m_errors;
	std::vector<crc_t> m_dups;
	std::vector<std::string> m_whitelist;
	std::vector<std::string> m_errvec;

	std::string m_declstr;
	std::string m_maskstr;
	std::string m_datafile = "../kabi-data.dat";
	std::string m_filelist = "kabi-datafiles.list";

	std::string m_startdir;
	std::string m_userdir;
	std::string m_kabidir = "redhat/kabi/";
	DIR *m_kbdir;

	crc_t m_crc;
	bool m_isfound = false;
	int m_count = 0;
	int m_flags = KB_QUIET;
	int m_errindex = 0;
	int m_exemask  = KB_COUNT | KB_DECL | KB_EXPORTS | KB_STRUCT;
};

#endif // KABILOOKUP_H
