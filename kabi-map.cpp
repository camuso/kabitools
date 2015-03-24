
/* kabi-map.cpp - multimap class for kabi-parser and kabi-lookup utilities
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

#include <map>
#include <cstring>
#include <fstream>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/map.hpp>

#include "checksum.h"
#include "kabi-map.h"

using namespace std;

Cqnodemap public_cqnmap;
dupmap_t dupmap;

static inline qnode* lookup_crc(unsigned long crc, qnodemap_t& qnmap)
{
	qniterator_t it = qnmap.lower_bound(crc);
	return it != qnmap.end() ? &(*it).second : NULL;
}

static inline qnode* alloc_qnode()
{
	qnode *qn = new qnode;
	return qn;
}

void insert_qnode(qnodemap_t& qnmap, struct qnode *qn)
{
	qnmap.insert(qnmap.end(), qnpair_t(qn->crc, *qn));
}

static inline void insert_cnode(cnodemap_t& cnmap, pair<unsigned long, int> cn)
{
	cnmap.insert(cnmap.begin(), cn);
}

static inline qnode* init_qnode(qnode *parent, qnode *qn, enum ctlflags flags)
{
	qn->name    = NULL;
	qn->symlist = NULL;
	qn->flags   = flags;

	qn->level = parent->level+1;
	return qn;
}

inline struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags)
{
	qnode *qn = alloc_qnode();
	init_qnode(parent, qn, flags);
	return qn;
}

// If no parent exists, then return NULL.
// If there is only one parent in the map, return the pointer to that.
// If there are more than one pointer, find the one with the children
// list having a size > 0. That's the primary parent of all duplicates
// in the file currently being processed.
struct qnode *qn_lookup_parent(unsigned long crc)
{
	qnodemap_t& qnmap = public_cqnmap.qnmap;
	qnitpair_t range = qnmap.equal_range(crc);
	qniterator_t qnit = range.first;

	int count = distance(range.first, range.second);

	if ((range.first == qnmap.end()) || (count < 1))
		return NULL;

	if (count == 1)
		return &(*qnit).second;

	qnit = find_if (range.first, range.second,
			[](qnpair_t& lqn) {
				return (lqn.second.children.size() > 0); });

	return qnit != range.second ? &(*qnit).second : NULL;
}

// The File is parent of all symbols found within it. The parent field
// will point to itself, but it will not be in the children map.
// All the exported functions in the file will be in the file node's
// children map.
struct qnode *new_firstqnode(char *file)
{
	// The "parent" of the File node is itself, so all the fields should
	// be identical, except that the parent of the File node has no
	// parents and only one child.
	struct qnode *parent = alloc_qnode();
	parent->name = NULL;
	parent->sdecl = string(file);
	parent->level = 0;
	parent->flags = CTL_FILE;
	parent->crc = raw_crc32(file);
	insert_cnode(parent->parents, make_pair(parent->crc, parent->level));

	struct qnode *qn = new_qnode(parent, parent->flags);
	qn->name  = parent->name;
	qn->sdecl = parent->sdecl;
	qn->level = parent->level;
	qn->crc   = parent->crc;
	update_qnode(qn, parent);
	return qn;
}

void update_qnode(struct qnode *qn, struct qnode *parent)
{
	qnode *pqn = qn_lookup_parent(parent->crc);
	pqn = pqn ? pqn : parent;

	// Record the parent's crc and level in this qnode's parents map.
	// Record this qnode's crc and level in the parent's children map.
	qn->sname = qn->name ? string(qn->name) : string("");
	insert_cnode(qn->parents, make_pair(parent->crc, parent->level));
	insert_cnode(pqn->children, make_pair(qn->crc, qn->level));
	insert_qnode(public_cqnmap.qnmap, qn);
}

void update_dupmap(qnode *qn)
{
	if (dupmap.find(qn->crc) == dupmap.end())
		dupmap.insert(dupair_t(qn->crc, *qn));
}

Cqnodemap& get_public_cqnmap()
{
	return public_cqnmap;
}

void delete_qnode(struct qnode *qn)
{
	delete qn;
}

struct qnode* qn_lookup_crc_other(unsigned long crc, Cqnodemap& Cqnmap)
{
	return lookup_crc(crc, Cqnmap.qnmap);
}

struct qnode* qn_lookup_crc(unsigned long crc)
{
	return lookup_crc(crc, public_cqnmap.qnmap);
}

void qn_add_to_decl(struct qnode *qn, char *decl)
{
	qn->sdecl += string(decl) + string(" ");
}

const char *cstrcat(const char *d, const char *s)
{
	if (!d)
		return s;
	if (!s)
		return d;
	string dd = string(d) + " " + string(s);
	return dd.c_str();
}
void qn_trim_decl(struct qnode *qn)
{
	qn->sdecl.erase(qn->sdecl.find_last_not_of(' ') + 1);
}

const char *qn_get_decl(struct qnode *qn)
{
	return qn->sdecl.c_str();
}

//static inline bool is_inlist(pair<unsigned long, int> cn, cnodemap_t& cnmap)
static inline bool is_inlist(cnpair_t& cn, cnodemap_t& cnmap)
{
	pair<cniterator_t, cniterator_t> range;
	range = cnmap.equal_range(cn.first);

	if (range.first == cnmap.end())
		return false;

	cniterator_t it = find_if (range.first, range.second,
				[&cn](pair<unsigned long, int> lcn)
				{ return lcn.second == cn.second; });
	return it != range.second;
}

static inline void update_duplicate(qnode *qn, qnode *parent)
{
	cnpair_t parentcn = make_pair(parent->crc, parent->level);
	if (!is_inlist(parentcn, qn->parents))
		insert_cnode(qn->parents, parentcn);

	cnpair_t childcn = make_pair(qn->crc, qn->level);
	if (!is_inlist(childcn, parent->children))
		insert_cnode(parent->children, childcn);
}

bool qn_is_dup(struct qnode *qn)
{
	bool retval = false;
	qnode* mapparent;

	qnodemap_t& qnmap = public_cqnmap.qnmap;
	qnitpair_t range = qnmap.equal_range(qn->crc);
	qniterator_t qnit = range.first;

	int count = distance(range.first, range.second);

	if ((range.first == qnmap.end()) || (count < 1))
		return false;

	if (count == 1) {
		mapparent = &(*qnit).second;
		update_duplicate(qn, mapparent);
		return true;
	}

	for_each (range.first, range.second,
		 [&qn, &retval](qnpair_t& lqn) {
			if (lqn.second.children.size() > 0) {
				update_duplicate(qn, &lqn.second);
				retval = true;
			}
		  });

	return retval;
}

void kb_write_dupmap(char *filename)
{
	ofstream ofs(filename, ofstream::out | ofstream::app);
	if (!ofs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		exit(1);
	}

	{
		boost::archive::text_oarchive oa(ofs);
		oa << dupmap;
	}
	ofs.close();

}

void kb_restore_dupmap(char *filename)
{
	ifstream ifs(filename);
	if (!ifs.is_open()) {
		fprintf(stderr, "File %s does not exist. A new file"
				" will be created\n.", filename);
		return;
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> dupmap;
	}
	ifs.close();
}

static inline void write_cqnmap(const char *filename, Cqnodemap& cqnmap)
{
	ofstream ofs(filename, ofstream::out | ofstream::app);
	if (!ofs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		exit(1);
	}

	{
		boost::archive::text_oarchive oa(ofs);
		oa << cqnmap;
	}
	ofs.close();
}

void kb_write_cqnmap_other(string& filename, Cqnodemap& cqnmap)
{
	write_cqnmap(filename.c_str(), cqnmap);
}

void kb_write_cqnmap(const char *filename)
{
	write_cqnmap(filename, public_cqnmap);
}

void kb_restore_cqnmap(char *filename)
{
	ifstream ifs(filename);
	if (!ifs.is_open()) {
		fprintf(stderr, "File %s does not exist. A new file"
				" will be created\n.", filename);
		return;
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> public_cqnmap;
	}
	ifs.close();
}

void kb_read_cqnmap(string filename, Cqnodemap &cqnmap)
{
	ifstream ifs(filename.c_str());
	if (!ifs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		exit(1);
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> cqnmap;
	}
	ifs.close();
}

bool kb_merge_cqnmap(char *filename)
{
	Cqnodemap cqm;
	qnodemap_t& cqm_nodes = cqm.qnmap;
	qnodemap_t& public_nodes = public_cqnmap.qnmap;

	ifstream ifs(filename);
	if (!ifs.is_open()) {
		return false;
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> cqm;
	}
	ifs.close();

	cqm_nodes.insert(public_nodes.begin(), public_nodes.end());

	remove(filename);
	string datafilename(filename);
	kb_write_cqnmap_other(datafilename, cqm);
	return true;
}

#include <boost/format.hpp>
using boost::format;

void kb_dump_cqnmap(char *filename)
{
	Cqnodemap cqq;
	qnodemap_t& qnmap = cqq.qnmap;

	kb_read_cqnmap(string(filename), cqq);

	cout << "map size: " << qnmap.size() << endl;

	for (auto it : qnmap) {
		qnpair_t qnp = it;
		qnode qn = qnp.second;

		if (qn.flags & CTL_FILE)
			cout << "FILE: ";

		cout << format("%10lu %08x %s ")
			% qnp.first % qn.flags % qn.sdecl;
		if (qn.flags & CTL_POINTER) cout << "*";
		cout << qn.sname << endl;

		cout << "\tparents: " << qn.parents.size() << endl;

		for_each (qn.parents.begin(), qn.parents.end(),
			 [](pair<const unsigned long, int>& lcn) {
				cout << format ("\t\t%10lu %3d\n")
					% lcn.first % lcn.second;
			  });

		if (!qn.children.size())
			goto bottom;

		cout << "\tchildren: " << qn.children.size() << endl;

		for_each (qn.children.begin(), qn.children.end(),
			 [](pair<const unsigned long, int>& lcn) {
				cout << format ("\t\t%10lu %3d\n")
					% lcn.first % lcn.second;
			  });
bottom:
		cout << endl;
	}
}

void kb_dump_qnode(struct qnode *qn)
{
	cout.setf(std::ios::unitbuf);
	cout << format("%08x %08x %03d %s %s\n")
		% qn->crc % qn->flags % qn->level % qn->sdecl % qn->sname;
	cout << format("\tparents: %3d   children: %3d\n")
		% qn->parents.size() % qn->children.size();
}
