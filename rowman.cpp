/* rowman.cpp - row manager for qrows
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

#include <iostream>
#include <algorithm>
#include <boost/format.hpp>
#include "qrow.h"
#include "rowman.h"

using namespace std;
using namespace boost;

rowman::rowman()
{
	dups.resize(LVL_COUNT);
}

void rowman::clear_dups(qrow& row)
{
	for (int i = row.level + 1; i < LVL_COUNT; ++i)
		dups.at(i).clear();
}

bool rowman::set_dup(qrow &row)
{
	int duplevel = row.level >= LVL_NESTED ? LVL_NESTED : row.level;
	qrow& dup = dups.at(duplevel);

	if (dup == row)
		return false;

	dup = row;
	return true;
}

bool rowman::is_dup(qrow &row)
{
	int duplevel = row.level >= LVL_NESTED ? LVL_NESTED : row.level;
	return dups.at(duplevel) == row;
}

void rowman::fill_row(const qnode& qn)
{
	qrow r;
	r.level = qn.level;
	r.order = qn.order;
	r.flags = qn.flags;
	r.decl = qn.sdecl;
	r.name = qn.sname;
	rows.push_back(r);
}

string &rowman::indent(int padsize)
{
	static string out;
	out.clear();
	while(padsize--) out += " ";
	return out;
}

void rowman::print_row(qrow& r, bool quiet)
{
	if (is_dup(r))
		return;

	switch (r.level) {
	case LVL_FILE:
		clear_dups();
		if (set_dup(r))
			cout << endl << "FILE: " << r.decl << endl;
		break;
	case LVL_EXPORTED:
		clear_dups(r);
		if (set_dup(r))
			cout << " EXPORTED: " << r.decl << " "
			     << r.name << endl;
		break;
	case LVL_ARG:
		clear_dups(r);
		if (set_dup(r)) {
			cout << ((r.flags & CTL_RETURN) ?
					 "  RETURN: " : "  ARG: ");
			cout << r.decl << " " << r.name << endl;
		}
		break;
	default:

		if(quiet && is_dup(dups[LVL_ARG]))
			return;

		if (set_dup(r) && !quiet)
			cout << indent(r.level) << r.decl << " "
			     << r.name << endl;
		break;
	}
}

void rowman::print_row_normalized(qrow& r, bool quiet)
{
	if (!m_normalized) {
		m_normalized = true;
		m_normalized_level = r.level;
	}

	int current_level = r.level - m_normalized_level;

	if (quiet && (current_level > 1))
		return;

	if (is_dup(r))
		return;

	if (set_dup(r)) {
		cout << indent(current_level) << r.decl;
		if ((current_level) > 0)
			cout << " " << r.name;
		cout << endl;
	}
}


void rowman::put_rows_from_back(bool quiet)
{
	cout << "\33[2K\r";

	for (auto it : rows) {
		qrow& r = rows.back();
		if (rows.size() == 1)
			print_row(r, false);
		print_row(r, quiet);
		rows.pop_back();
	}
}

void rowman::put_rows_from_front(bool quiet)
{
	cout << "\33[2K\r";

	sort(rows.begin(), rows.end(), [](qrow const& a, qrow const& b)
	{
		if (a.order < b.order) return true;
		if (a.order > b.order) return false;
		return false;
	});

	for (auto it : rows) {
		if (it == rows.back())
			print_row(it, false);
		print_row(it, quiet);
	}
}

void rowman::put_rows_from_back_normalized(bool quiet)
{
	cout << "\33[2K\r";

	sort(rows.begin(), rows.end(), [](qrow const& a, qrow const& b)
	{
		if (a.order > b.order) return true;
		if (a.order < b.order) return false;
		return false;
	});

	for (auto it : rows) {
		qrow& r = rows.back();
		print_row_normalized(r, quiet);
		rows.pop_back();
	}
	cout << endl;
}

void rowman::put_rows_from_front_normalized(bool quiet)
{
	cout << "\33[2K\r";

	for (auto it : rows)
		print_row_normalized(it, quiet);

	cout << endl;
}
