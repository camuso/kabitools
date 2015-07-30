#ifndef ROWMAN_H
#define ROWMAN_H

/* rowman.h - row manager for qrows
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

#include <string>
#include <vector>
#include <map>
#include "kabi-map.h"
#include "qrow.h"

typedef std::vector<qrow> rowvec_t;

class rowman
{
public:
	rowman();
	rowvec_t rows;

	void fill_row(const dnode &dn, const cnode& qn);
	void put_rows_from_back(bool quiet = false);
	void put_rows_from_front(bool quiet = false);
	void put_rows_from_back_normalized(bool quiet = false);
	void put_rows_from_front_normalized(bool quiet = false);
	void print_row(qrow& r, bool quiet = false);

private:
	std::string &indent(int padsize);
	void print_row_normalized(qrow& r, bool quiet = false);
	bool set_dup(qrow& row);
	bool is_dup(qrow& row);
	void clear_dups() { dups.clear(); dups.resize(LVL_COUNT); }
	void clear_dups(qrow& row);
	std::string get_name(qrow& row);

	rowvec_t dups;
	bool m_normalized = false;
	int m_normalized_level;
};


#endif // ROWMAN_H
