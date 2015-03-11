/* kabi-node.cpp
 *
 * Copyright (C) 2015  Red Hat Inc.
 * Tony Camuso <tcamuso@redhat.com>
 *
 ********************************************************************************
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
 *******************************************************************************
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
	return qn;
}

void update_qnode(struct qnode *qn)
{
	qn->sname   = qn->name   ? string(qn->name)   : string("");
	qn->stypnam = qn->typnam ? string(qn->typnam) : string("");
	qn->sfile   = qn->file   ? string(qn->file)   : string("");
	cq.qnodelist.push_back(*qn);
}

vector<qnode> &get_qnodelist()
{
	return cq.qnodelist;
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

struct qnode *qn_lookup_crc(unsigned long crc)
{
	for (unsigned i = 0; i < cq.qnodelist.size(); ++i)
		if (cq.qnodelist[i].cn->crc == crc)
			return &(cq.qnodelist[i]);
	return NULL;
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

bool qn_is_dup(struct qnode *qn, struct qnode* parent, unsigned long crc)
{
	struct qnode *top = qn_lookup_crc(crc);

	if (top) {
		qn_add_parent(top, parent);
		parent->children.pop_back();
		delete_qnode(qn);
		return true;
	}
	return false;
}

const char *cstrcat(const char *d, const char *s)
{
	string dd = string(d) + string(s);
	return dd.c_str();
}

void kb_write_qlist(char *filename)
{
	ofstream ofs(filename, ofstream::out | ofstream::app);
	if (!ofs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		exit(1);
	}

	{
		boost::archive::text_oarchive oa(ofs);
		oa << cq;
	}
	ofs.close();
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
