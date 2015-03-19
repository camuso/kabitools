
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

#include "kabi-map.h"

using namespace std;

Cqnodemap public_cqnmap;

static inline qnode* alloc_qnode()
{
	qnode *qn = new qnode;
	return qn;
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

struct qnode *new_firstqnode(char *file,
			     enum ctlflags flags,
			     struct qnode **pparent)
{
	*pparent = alloc_qnode();
	struct qnode *parent = *pparent;
	parent->sdecl = string(file);
	parent->level = 0;
	parent->flags = CTL_FILE;
	parent->crc = 0;
	public_cqnmap.qnmap.insert(qnpair_t(parent->crc, *parent));
	parent->parents.insert(make_pair(parent->crc, parent->level));
	return new_qnode(parent, flags);
}

void update_qnode(struct qnode *qn, struct qnode *parent)
{
	qn->parents.insert(make_pair(parent->crc, parent->level+1));
	parent->children.insert(make_pair(qn->crc, qn->level));
	qn->sname   = qn->name   ? string(qn->name)   : string("");
	public_cqnmap.qnmap.insert(qnpair_t(qn->crc, *qn));
}

Cqnodemap& get_public_cqnmap()
{
	return public_cqnmap;
}

void delete_qnode(struct qnode *qn)
{
	delete qn;
}

static inline qnode* lookup_crc(unsigned long crc, qnodemap_t& qnmap)
{
	qniterator_t it = qnmap.find(crc);
	return it != qnmap.end() ? &(*it).second : NULL;
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

void qn_trim_decl(struct qnode *qn)
{
	qn->sdecl.erase(qn->sdecl.find_last_not_of(' ') + 1);
}

const char *qn_get_decl(struct qnode *qn)
{
	return qn->sdecl.c_str();
}

static inline void insert_cnode(cnodemap_t& cnmap, pair<unsigned, int> cn)
{
	cnmap.insert(cn);
}

static inline bool is_inlist(pair<unsigned, int> cn, cnodemap_t& cnmap)
{
	pair<cniterator_t, cniterator_t> range;
	range = cnmap.equal_range(cn.first);

	if (range.first == cnmap.end())
		return false;

	cniterator_t it = find_if (range.first, range.second,
		  [&cn](pair<unsigned, int> lcn)
			{ return lcn.second == cn.second; });
	return it != range.second;
}

static inline void update_duplicate(qnode *qn, qnode *parent)
{
	pair<unsigned, int> parentcn = make_pair(parent->crc, parent->level);
	if (!is_inlist(parentcn, qn->parents))
		insert_cnode(qn->parents, parentcn);

	pair<unsigned, int> childcn = make_pair(qn->crc, parent->level);
	if (!is_inlist(childcn, parent->children))
		insert_cnode(parent->children, childcn);
}

bool qn_is_dup(struct qnode *qn, struct qnode* parent)
{
	bool retval = false;
	qnodemap_t& qnmap = public_cqnmap.qnmap;
	pair<qniterator_t, qniterator_t> range;
	range = qnmap.equal_range(qn->crc);

	if (range.first == qnmap.end())
		return false;

	for_each (range.first, range.second,
		 [&qn, &parent, &retval](qnpair_t& lqn) {
			if (lqn.first == qn->crc) {
				qnode* mapparent = qn_lookup_crc(parent->crc);
				update_duplicate(&lqn.second, mapparent);
				retval = true;
			}
		  });

	return retval;
}

const char *cstrcat(const char *d, const char *s)
{
	string dd = string(d) + string(s);
	return dd.c_str();
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

	ifstream ifs(filename);
	if (!ifs.is_open()) {
		return false;
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> cqm;
	}
	ifs.close();

	for (auto it : public_cqnmap.qnmap)
		cqm.qnmap.insert(it);

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

		cout << format("%08x %08x %s ")
			% qnp.first % qn.flags % qn.sdecl;
		if (qn.flags & CTL_POINTER) cout << "*";
		cout << qn.sname << endl;

		cout << "\tparents" << endl;

		for_each (qn.parents.begin(), qn.parents.end(),
			 [](pair<const unsigned long, int>& lcn) {
				cout << format ("\tcrc: %08x level: %d\n")
					% lcn.first % lcn.second;
			  });

		if (!qn.children.size())
			goto bottom;

		cout << "\tchildren" << endl;
		for_each (qn.children.begin(), qn.children.end(),
			 [](pair<const unsigned long, int>& lcn) {
				cout << format ("\tcrc: %08x level: %d\n")
					% lcn.first % lcn.second;
			  });
bottom:
		cout << endl;
	}
}
