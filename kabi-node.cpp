/* kabi-node.cpp - node class for kabi-parser and kabi-lookup utilities
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

#include <vector>
#include <cstring>
#include <fstream>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>

#include "kabi-node.h"

using namespace std;

Cqnodelist cq;

qnode *alloc_qnode()
{
	qnode *qn = new qnode;
	cnode *cn = new cnode;
	qn->cn = cn;
	return qn;
}

qnode *init_qnode(qnode *parent, qnode *qn, enum ctlflags flags)
{
	qn->name    = NULL;
	qn->typnam  = NULL;
	qn->file    = NULL;
	qn->symlist = NULL;
	qn->flags   = flags;

	qn->cn->level = parent->cn->level + 1;
	qn->parents.push_back(*(parent->cn));
	parent->children.push_back(*(qn->cn));
	return qn;
}

struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags)
{
	qnode *qn = alloc_qnode();
	init_qnode(parent, qn, flags);
	return qn;
}

struct qnode *new_firstqnode(enum ctlflags flags)
{
	struct qnode *qn = alloc_qnode();
	init_qnode(qn, qn, flags);
	qn->parents[0].level = 0;
	return qn;
}

void update_qnode(struct qnode *qn)
{
	qn->sname   = qn->name   ? string(qn->name)   : string("");
	qn->stypnam = qn->typnam ? string(qn->typnam) : string("");
	qn->sfile   = qn->file   ? string(qn->file)   : string("");
	cq.qnodelist.push_back(*qn);
//	cq.sublist.push_back(*qn);
}

Cqnodelist &get_qnodelist()
{
	return cq;
}

void delete_qnode(struct qnode *qn)
{
	delete qn->cn;
	delete qn;
}

void qn_add_parent(struct qnode *qn, struct qnode *parent)
{
	qn->parents.push_back(*(parent->cn));
}

void qn_add_child(struct qnode *qn, struct qnode *child)
{
	qn->children.push_back(*(child->cn));
}

static inline qnode *lookup_crc(unsigned long crc, vector<qnode>& qlist)
{
	vector<qnode>::iterator it;
	for (it = qlist.begin(); it < qlist.end(); ++it) {
		qnode *qn = &(*it);
		if (qn->cn->crc == crc)
			return qn;
	}
	return NULL;
}

struct qnode *qn_lookup_crc_slist(unsigned long crc)
{
	return lookup_crc(crc, *cq.duplist);
}

struct qnode *qn_lookup_crc_other(unsigned long crc, Cqnodelist& qnlist)
{
	return lookup_crc(crc, qnlist.qnodelist);
}

struct qnode *qn_lookup_crc(unsigned long crc)
{
	return lookup_crc(crc, cq.qnodelist);
}

bool qn_lookup_parent(struct qnode *qn, unsigned long crc)
{
	qnode q = *qn;
	for (unsigned i = 0; i < q.parents.size(); ++i)
		if (q.parents[i].crc == crc)
			return true;
	return false;
}

bool qn_lookup_child(struct qnode *qn, unsigned long crc)
{
	qnode q = *qn;
	for (unsigned i = 0; i < q.children.size(); ++i)
		if (q.children[i].crc == crc)
			return true;
	return false;
}

void qn_add_to_declist(struct qnode *qn, char *decl)
{
	qn->sdecl += string(decl) + string(" ");
}

const char *qn_extract_type(struct qnode *qn)
{
	return qn->sdecl.c_str();
}

static inline bool is_inlist(cnode *cn, vector<cnode>& cnlist)
{
	vector<cnode>::iterator it;
	for (it = cnlist.begin(); it < cnlist.end(); ++it) {
		cnode *pcn = &(*it);
		if ((pcn->crc == cn->crc) && (pcn->level == cn->level))
			return true;
	}
	return false;
}

static inline void update_duplicate(qnode *top, qnode *parent)
{
	if (!is_inlist(parent->cn, top->parents))
		qn_add_parent(top, parent);

	if (!is_inlist(top->cn, parent->children))
		qn_add_child(parent, top);
}

bool qn_is_dup(struct qnode *qn, struct qnode* parent, unsigned long crc)
{
	struct qnode *top = qn_lookup_crc(crc);

	if (top) {
		update_duplicate(top, parent);
		delete_qnode(qn);
		return true;
	}
	return false;
}

// qn_is_duplist - find duplicate and move parent list
//
// If we find that the qnode argument is a duplicate:
// . walk the parent list of the duplicate qnode
// . move its parents to the parents list of the original qnode,
// . add the original qnode to the children list of each parent.
//
bool qn_is_duplist(qnode *qn, vector<qnode>& qlist)
{
	qnode *parent;
	qnode *top = lookup_crc(qn->cn->crc, qlist);

	if (top) {
		vector<cnode>::iterator it;
		for (it = qn->parents.begin(); it < qn->parents.end(); ++it) {
			cnode *pcn = &(*it);
			if ((parent = lookup_crc(pcn->crc, qlist)))
				update_duplicate(top, parent);
		}
		return true;
	}
	return false;
}

const char *cstrcat(const char *d, const char *s)
{
	string dd = string(d) + string(s);
	return dd.c_str();
}

static inline void write_qlist(const char *filename, Cqnodelist& qnlist)
{
	ofstream ofs(filename, ofstream::out | ofstream::app);
	if (!ofs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		exit(1);
	}

	{
		boost::archive::text_oarchive oa(ofs);
		oa << qnlist;
	}
	ofs.close();
}

void kb_write_qlist_other(string& filename, Cqnodelist& qnlist)
{
	write_qlist(filename.c_str(), qnlist);
}

void kb_write_qlist(const char *filename)
{
	using namespace boost;
	write_qlist(filename, cq);
}

void kb_restore_qlist(char *filename)
{
	ifstream ifs(filename);
	if (!ifs.is_open()) {
		fprintf(stderr, "File %s does not exist. A new file"
				" will be created\n.", filename);
		return;
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> cq;
	}
	ifs.close();
}

void kb_read_qlist(string filename, Cqnodelist &qlist)
{
	ifstream ifs(filename.c_str());
	if (!ifs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		exit(1);
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> qlist;
	}
	ifs.close();
}

#include <boost/format.hpp>
using boost::format;

void kb_dump_qlist(char *filename)
{
	Cqnodelist cqq;

	kb_read_qlist(string(filename), cqq);

	for (unsigned j = 0; j < cqq.qnodelist.size(); ++j) {
		qnode *qn = &cqq.qnodelist[j];
		cout << "file: " << qn->sfile << endl;
		cout << format("crc: %08x flags: %08x decl: %s")
			% qn->cn->crc % qn->flags % qn->sdecl;
		if (qn->flags & CTL_POINTER) cout << "*";
		cout << qn->sname << endl;

		cout << "\tparents" << endl;
		for (unsigned k = 0; k < qn->parents.size(); ++k) {
			struct cnode *cn = &qn->parents[k];
			cout << format ("\tcrc: %08x level: %d\n")
				% cn->crc % cn->level;
		}
	}
}
