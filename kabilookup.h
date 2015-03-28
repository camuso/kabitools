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
#include "kabi-map.h"
#include "options.h"
#include "error.h"
#include "rowman.h"

struct row {
	int level;
	int flags;
	std::string file;
	std::string decl;
	std::string name;
	enum rowflags rowflags;
};

enum rowpolicy {
	ROW_ACCUM,
	ROW_FLUSH,
};

class lookup
{
public:
	lookup(){}
	lookup(int argc, char **argv);
	int run();
	static std::string get_helptext();
private:
	int process_args(int argc, char **argv);
	bool check_flags();
	int count_bits(unsigned mask);
	std::string &pad_out(int padsize);
	void put_row(row& r);
	void put_rows_from_back();
	void put_rows_from_front();
	void fill_row(const qnode& qn, enum rowpolicy rowpol=ROW_ACCUM);
	bool find_decl(qnode& qnr, std::string decl);
	bool get_qnrange(unsigned long crc, qnpair_t& range);
	qnode* find_qnode_nextlevel(qnode* qn, long unsigned crc, int level);
	int get_decl_list(std::vector<qnode>& retlist);
	void show_spinner(int& count);
	int get_parents(qnode& qn);
	int get_children_wide(qnode& qn);
	int get_children_deep(qnode& parent, cnpair_t& cn);
	int execute(std::string datafile);
	int exe_count();
	int exe_struct();
	int exe_exports();
	int exe_decl();

	// member classes
	Cqnodemap& m_cqnmap = get_public_cqnmap();
	options m_opts;
	error m_err;
	qnode m_qn;
	qnode* m_qnp = &m_qn;
	qnode& m_qnr = m_qn;

	// member basetypes
	cnodemap_t* m_cnmap;
	qnodemap_t& m_qnodes = m_cqnmap.qnmap;
	typedef std::pair<int, std::string> errpair;

	std::vector<row> m_rows;
	std::vector<errpair> m_errors;
	std::string m_datafile = "../kabi-data.dat";
	std::string m_filelist = "./redhat/kabi/parser/kabi-files.list";
	std::string m_declstr;

	unsigned long m_crc;
	bool m_isfound = false;
	int m_count = 0;
	int m_flags;
	int m_errindex = 0;
	int m_exemask  = KB_COUNT | KB_DECL | KB_EXPORTS | KB_STRUCT;
};

#endif // KABILOOKUP_H
