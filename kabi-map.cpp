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

#define NDEBUG

using namespace std;

Cqnodemap public_cqnmap;

void cnode::operator = (const cnode& cn)
{
	function = cn.function;
	level = cn.level;
}

bool cnode::operator ==(const cnode& cn) const
{
	return ((function == cn.function) && (level == cn.level));
}

void dnode::operator = (const dnode& dn)
{
	decl = dnode.decl;
	parents.insert(dn.parents.begin(), dn.parents.end());
	siblings.insert(dn.siblings.begin(), dn.siblings.end());
	children.insert(dn.children.begin(), dn.children.end());
}

bool dnode::operator ==(const dnode& dn) const
{
	return (decl == dn.decl);
}

//static inline qnode* lookup_crc(unsigned long crc, qnodemap_t& qnmap)
//{
//	qniterator_t it = qnmap.lower_bound(crc);
//	return it != qnmap.end() ? &(*it).second : NULL;
//}

static inline qnode* alloc_qnode()
{
	qnode *qn = new qnode;
	dnode *dn = new dnode;
	qn->dnode = (void*)dnode;
	return qn;
}

static inline cnode* alloc_cnode(pnode_t& function,
				 pnode_t& ancestor,
				 int level,
				 ctlflags flags,
				 string name)
{
	cnode *cn = new cnode(function, ancestor, level, flags, name);
	return cn;
}

void insert_dnode(dnodemap_t& dnmap, struct qnode *qn)
{
	dnmap.insert(dnmap.end(), dnpair_t(qn->crc, *(qn->dn)));
}

static inline void insert_cnode(cnodemap_t& cnmap, pair<crc_t, cnode> cn)
{
	cnmap.insert(cnmap.end(), cn);
}

static inline qnode* init_qnode(qnode *parent, qnode *qn, enum ctlflags flags)
{
	qn->name    = NULL;
	qn->symlist = NULL;
	qn->flags   = flags;
	qn->level = parent->level+1;
	return qn;
}

/******************************************************************************
 * init_crc(const char *decl, qnode *qn, qnode *parent)
 *
 * decl   - declaration of type to be converted to crc
 * qn     - qnode struct containing the details of this instance of this symbol
 * parent - parent qnode
 *
 * See the commentary in kabi-map.h to better understand the reasoning
 * behind the logic of this function.
 *
 */
void init_crc(const char *decl, struct qnode *qn, struct qnode *parent)
{
	qn->crc = raw_crc32(decl);

	if (parent->flags & (CTL_ARG | CTL_RETURN))
		qn->ancestor = parent->crc;
	else
		qn->ancestor = parent->ancestor;

	if ((qn->flags & CTL_FUNCTION) && (qn->flags & CTL_EXPORTED))
		qn->function = qn->crc;
	else
		qn->function = parent->function;
}

void init_ancestor(unsigned long crc, struct cnode *cn)
{
	cn->ancestor = pnode_t(crc, cn->level);
}

inline struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags)
{
	qnode *qn = alloc_qnode();
	init_qnode(parent, qn, flags);
	return qn;
}

/****************************************************************************
 * kb_lookup_dnode(qnode *qn, unsigned long crc)
 *
 * Lookup a dnode in the map given either a parent or child and the crc
 * of the qnode being sought.
 *
 * dn  - pointer to the parent or child qnode from which to start the search
 * crc - the crc of the symbol encapsulated by the dnode we are looking for
 * dir - search up (for parent) or down (for child). Default is up.
 *       See enum qnseek and the function prototype in kabi-map.h
 *
 * If we can't find it, then return NULL.
 * If there is only one instance of the qnode in the map, return its pointer.
 * If there is more than one instance, find the one having the same ancestor
 * or function, depending on the level we are searching from.
 *
 */
extern dnode* kb_lookup_qnode(dnode* dn, crc_t crc, qnseek dir)
{
	dnodemap_t& dnmap = public_dnmap.dnmap;
	dnitpair_t  range = dnmap.equal_range(crc);
	dniterator_t dnit = range.first;

	// The direction in which we are searching determines the boundary
	// at which we search for the matching function instead of the
	// matching arg or return. Arg and return symbols must point back
	// to the functions from which they are descended, and nested symbols
	// must point back to the arg or return symbol from which they are
	// descended.
	int lvlbound = (dir == QN_UP) ? LVL_NESTED : LVL_ARG;

	int count = distance(range.first, range.second);

	if ((range.first == qnmap.end()) || (count < 1))
		return NULL;

	if (count == 1)
		return &(*dnit).second;

	dnit = find_if (range.first, range.second,
		[&dn, &dir, &lvlbound](dnpair_t& ldn)
		{
			qnode& rdn = ldn.second;

			if (dn->level > lvlbound)
				return ((rdn.level == dn->level - dir ) &&
					(rdn.ancestor == dn->ancestor));
			else
				return ((rdn.level == dn->level - dir) &&
					(rdn.function == dn->function));
		});

	return dnit != range.second ? &(*dnit).second : NULL;
}

/****************************************************************************
 * new_firstqnode(char *file)
 *
 * Returns new first qnode
 *
 * The File is parent of all symbols found within it. The parent field
 * will point to itself, but it will not be in the children map.
 * All the exported functions in the file will be in the file node's
 * children map.
 */
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
	parent->ancestor = pnode_t(parent->crc, 0);

	struct qnode *qn = new_qnode(parent, parent->flags);
	qn->name  = parent->name;
	qn->sdecl = parent->sdecl;
	qn->level = parent->level;
	qn->crc   = parent->crc;
	qn->ancestor = parent->ancestor;
	update_qnode(qn, parent);
	return qn;
}

void update_qnode(struct qnode *qn, struct qnode *parent)
{
	// Now's a good time to create the cnode for this qnode and
	// insert into the parent's children map.
	cnode *cn = alloc_cnode(qn->function, qn->level, qn->name);
	insert_cnode(parent->children, make_pair(qn->crc, *cn));

	// And while we're at it, create the parent cnode and insert
	// that into this qnode's parents map.
	cnode *pcn = alloc_cnode(parent->function, parent->level, parent->name);
	insert_cnode(qn->parents, make_pair(parent->crc, *pcn));

	insert_qnode(public_cqnmap.qnmap, qn);
}

Cqnodemap& get_public_cqnmap()
{
	return public_cqnmap;
}

void delete_qnode(struct qnode *qn)
{
	delete qn;
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

static inline bool is_inlist(cnpair_t& cnp, cnodemap_t& cnmap)
{
	pair<cniterator_t, cniterator_t> range;
	range = cnmap.equal_range(cnp.first);

	if (range.first == cnmap.end())
		return false;

	cniterator_t it = find_if (range.first, range.second,
				[&cnp](cnpair_t lcnp)
				{	cnode& lcn = lcnp.second;
					cnode& cn = cnp.second;
					return lcn.operator ==(cn);
				});

	return it != range.second;
}

static inline void update_duplicate(qnode *qn, qnode *parent)
{
	cnode* cn = alloc_cnode(qn->function, qn->level, qn->name);
	cnpair_t childcn = make_pair(qn->crc, *cn);
	if (!is_inlist(childcn, parent->children))
		insert_cnode(parent->children, childcn);
	else
		delete cn;

	cnode *pcn = alloc_cnode(parent->function, parent->level, parent->name);
	cnpair_t parentcn = make_pair(parent->crc, *pcn);
	if (!is_inlist(parentcn, qn->parents))
		insert_cnode(qn->parents, parentcn);
	else
		delete pcn;
}

bool qn_is_dup(struct qnode *qn, struct qnode *parent)
{
	bool dup = false;
	qnodemap_t& qnmap = public_cqnmap.qnmap;
	qnitpair_t range = qnmap.equal_range(qn->crc);
	qniterator_t qnit;
	int count = distance(range.first, range.second);

	if ((qn->level < LVL_NESTED) || (count == 0))
		return false;

	// This is a dup only if it has the same ancestor as well as the
	// same crc signature.
	qnit = find_if (range.first, range.second,
		[&qn](qnpair_t lqn)
		{
			return lqn.second.function == qn->function;
		});

	if ((dup = (qnit != range.second))) {
		dup = true;
		update_duplicate(qn, parent);
	}

	return dup;
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

int kb_read_cqnmap(string filename, Cqnodemap &cqnmap)
{
	ifstream ifs(filename.c_str());
	if (!ifs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		return(-1);
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> cqnmap;
	}
	ifs.close();
	return 0;
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


void static inline dump_cnmap(cnodemap_t& cnmap)
{
	for (auto i : cnmap) {
		cnode& cn = i.second;
		cout << format("\t\t  %12lu %3d %12lu %3d")
			% cn.function.first % cn.function.second
			% i.first % cn.level;
		if (cn.flags & CTL_POINTER)
			cout << " *";
		if (cn.name.size() > 0)
			cout << " " << cn.name;
		cout << endl;
	}
}


int kb_dump_cqnmap(char *filename)
{
	Cqnodemap cqq;
	qnodemap_t& qnmap = cqq.qnmap;

	if (int retval = kb_read_cqnmap(string(filename), cqq) != 0)
		return retval;

	cout << "map size: " << qnmap.size() << endl;

	for (auto it : qnmap) {
		qnpair_t qnp = it;
		qnode qn = qnp.second;

		if (qn.flags & CTL_FILE)
			cout << "FILE: ";

		cout << format("%12lu %3d %08x %s ")
			% qnp.first %qn.level % qn.flags % qn.sdecl;

		cout << format("\tancestor: %12lu %3d\n")
			% qn.ancestor.first % qn.ancestor.second;

		cout << format("\tfunction: %12lu %3d\n")
			% qn.function.first % qn.function.second;

		cout << "\tparents: " << qn.parents.size() << endl;

		if (qn.parents.size() > 0)
			dump_cnmap(qn.parents);

		cout << "\tchildren: " << qn.children.size() << endl;

		if (qn.children.size() > 0)
			dump_cnmap(qn.children);

		cout << endl;
	}
	return 0;
}

void kb_dump_qnode(struct qnode *qn)
{
	cout.setf(std::ios::unitbuf);
	cout << format("%08x %08x %03d %s\n")
		% qn->crc % qn->flags % qn->level % qn->sdecl;
}
