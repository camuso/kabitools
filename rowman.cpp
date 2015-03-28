#include <iostream>
#include <boost/format.hpp>
#include "qrow.h"
#include "rowman.h"

using namespace std;
using namespace boost;

rowman::rowman()
{
	dups.resize(LVL_COUNT);
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
	qrow& dup = dups.at(duplevel);
	return (dup == row);
}

void rowman::fill_row(const qnode& qn)
{
	qrow r;
	r.level = qn.level;
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

void rowman::print_row(qrow& r, bool verbose)
{
	if (is_dup(r))
		return;

	switch (r.level) {
	case LVL_FILE:
		clear_dups();
		if (set_dup(r))
			cout << "FILE: " << r.decl << endl;
		break;
	case LVL_EXPORTED:
		if (set_dup(r))
			cout << " EXPORTED: " << r.decl << " "
			     << r.name << endl;
		break;
	case LVL_ARG:
		if (set_dup(r)) {
			cout << ((r.flags & CTL_RETURN) ?
					 "  RETURN: " : "  ARG: ");
			cout << r.decl << " " << r.name << endl;
		}
		break;
	default:
		if (set_dup(r) && verbose)
			cout << indent(r.level) << r.decl << " "
			     << r.name << endl;
		break;
	}
}

void rowman::put_rows_from_back()
{
	for (auto it : rows) {
		qrow& r = rows.back();
		print_row(r);
		rows.pop_back();
	}
	cout << endl;
}

void rowman::put_rows_from_front()
{
	for (auto it : rows)
		print_row(it);

	cout << endl;
}
