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

dnodemap public_dnodemap;

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

/******************************************************************************
 * bool has_same_ancestry(dnode& dn, cnode& cn)
 *
 * dn - dnode (declaration node) that abstracts the cnode's data type
 * cn - the specific instance of this data type
 *
 * Returns true if dnode has parents that share the same ancestry as
 * the cnode.
 *
 * Look into the parents map of a qnode to see if any of the parents
 * there share the same ancestry as the cn parameter.
 */
static bool has_same_ancestry(dnode& dn, cnode& cn)
{
	cnodemap& parents = dnode.parents;
	cniterator cnit = find_if(parents.begin(), parents.end(),
		[&cn](cnpair lcnp)
		{
			cnode& lcn = lcnp.second;
			return (lcn.function == cn.function);
		});

	return (cnit != parents.end());
}

static dnode* lookup_dnode(cnpair& cnp)
{
	dnodemap& dnmap = public_dnodemap;
	dnitpair  range = dnmap.equal_range(cnp.first);
	dniterator dnit = range.first;
	int count = distance(range.first, range.second);

	if ((count < 1) || (range.first == dnmap.end()))
		return NULL;

	if (count == 1)
		return &(*dnit).second;

	dnit = find_if (range.first, range.second,
		[&cnp](dnpair& ldn)
		{
			dnode& rdn = ldn.second;
			cnode& cn = cnp.second;

			return has_same_ancestry(rdn, cn);
		});

	return dnit != range.second ? &(*dnit).second : NULL;
}

/******************************************************************************
 * alloc_qnode
 *
 * This is the object that conveys information from the parser to the c++
 * side of the world.
 */
static inline sparm* alloc_sparm()
{
	sparm *sp = new sparm;
	dnode *dn = new dnode;
	sp->dnode = (void*)dnode;
	return sp;
}

static inline cnode* alloc_cnode(edgepair& function,
				 edgepair& argument,
				 int level,
				 int order,
				 ctlflags flags,
				 string name)
{
	cnode *cn = new cnode(function, argument, order, level, flags, name);
	return cn;
}

static inline void insert_dnode(dnodemap& dnmap, struct sparm *sp)
{
	dnmap.insert(dnmap.end(), dnpair(sp->crc, *(dnode*)(sp->dnode)));
}

static inline void insert_cnode(cnodemap& cnmap, pair<crc_t, cnode> cn)
{
	cnmap.insert(cnmap.end(), cn);
}

static inline
sparm* init_sparm(sparm* parent, sparm *sp, enum ctlflags flags)
{
	sp->name    = NULL;
	sp->symlist = NULL;
	sp->flags   = flags;
	sp->level = parent->level+1;
	return sp;
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
static inline void
kb_init_crc(const char *decl, struct sparm *sp, struct sparm *parent)
{
	sp->crc = raw_crc32(decl);

	if (parent->flags & (CTL_ARG | CTL_RETURN))
		sp->argument = parent->crc;
	else
		sp->argument = parent->argument;

	if ((sp->flags & CTL_FUNCTION) && (sp->flags & CTL_EXPORTED))
		sp->function = sp->crc;
	else
		sp->function = parent->function;
}

struct sparm *kb_new_sparm(struct sparm *parent, enum ctlflags flags)
{
	sparm *sp = alloc_sparm();
	init_sparm(parent, sp, flags);
	return sp;
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
struct sparm *kb_new_firstsparm(char *file, int order)
{
	// The "parent" of the File node is itself, so all the fields should
	// be identical, except that the parent of the File node has no
	// parents and only one child.
	struct sparm *parent = alloc_sparm();
	parent->name = NULL;
	parent->decl = string(file);
	parent->level = 0;
	parent->order = order;
	parent->flags = CTL_FILE;
	parent->crc = raw_crc32(file);
	parent->argument = 0;
	parent->function = 0;

	struct sparm *sp = kb_new_sparm(parent, parent->flags);
	sp->name  = parent->name;
	sp->sdecl = parent->decl;
	sp->level = parent->level;
	sp->order = parent->order;
	sp->crc   = parent->crc;
	sp->function = parent->function;
	sp->argument = parent->argument;
	kb_update_nodes(sp, parent);
	return qn;
}

void kb_update_nodes(struct sparm *sp, struct sparm *parent)
{
	// get the dnodes from the sparm
	dnode* dn = (dnode *)sp->dnode;
	dnode* pdn = (dnode *)parent->dnode;

	cnode *cn = alloc_cnode(sp->function, sp->argument, sp->level,
				sp->order, sp->flags, sp->name);

	cnpair cnp = make_pair(sp->crc, *cn);
	insert_cnode(pdn->children, cnp);

	if (sp->flags & CTL_ISDUP) {
		dnode* sib = lookup_dnode(cnp);
		insert_cnode(sib->siblings, cnp);
		return;
	}

	insert_cnode(dn->siblings, cnp);

	cnode *pcn = alloc_cnode(parent->function, parent->argument,
				 parent->level, parent->order,
				 parent->flags, parent->name);

	insert_cnode(dn->parents, make_pair(parent->crc, *pcn));
	insert_dnode(public_dnodemap, dn);
}

dnodemap& kb_get_public_dnodemap()
{
	return public_dnodemap;
}

const char *kb_cstrcat(const char *d, const char *s)
{
	if (!d)
		return s;
	if (!s)
		return d;
	string dd = string(d) + " " + string(s);
	return dd.c_str();
}

void kb_add_to_decl(struct sparm *qn, char *decl)
{
	dnode* dn = (dnode *)sp->dnode;
	if (dn->decl.size() != 0)
		dn->decl += " ";
	dn->decl += string(decl);
}

void kb_trim_decl(struct sparm *sp)
{
	dnode* dn = (dnode *)sp->dnode;
	dn->decl.erase(qn->sdecl.find_last_not_of(' ') + 1);
}

const char *kb_get_decl(struct sparm *sp)
{
	dnode* dn = (dnode *)sp->dnode;
	return dn->decl.c_str();
}

bool kb_is_dup(struct sparm *sp, struct sparm *parent)
{
	bool dup = false;
	dnode* dn = (dnode *)sp->dnode;
	dnodemap& dnmap = public_dnodemap;
	dnitpair range = dnmap.equal_range(sp->crc);
	int count = distance(range.first, range.second);

	if ((sp->level < LVL_NESTED) || (count == 0))
		return false;

	// This is a dup only if it has the same ancestor as well as the
	// same crc signature.
	dniterator dnit = find_if (range.first, range.second,
		[&dn](dnpair ldn)
		{
			return ldn.second.function == dn->function;
		});

	if (dnit != range.second)
		sp->flags |= CTL_ISDUP;

	return dup;
}

static inline void write_cqnmap(const char *filename, Cqnodemap& cqnmap)
{
	ofstream ofs(filename, ofstream::out | ofstream::app);
	if (!ofs.is_open()) {
		cout << "Cannot open file: " << filename <parent< endl;
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

#if 0
static inline bool is_inlist(cnpair& cnp, cnodemap& cnmap)
{
	pair<cniterator, cniterator> range;
	range = cnmap.equal_range(cnp.first);

	if (range.first == cnmap.end())
		return false;

	cniterator it = find_if (range.first, range.second,
				[&cnp](cnpair lcnp)
				{	cnode& lcn = lcnp.second;
					cnode& cn = cnp.second;
					return lcn.operator ==(cn);
				});

	return it != range.second;
}

static inline void update_duplicate(sparm *sp, sparm *parent)
{
	dnode* dn = (dnode *)sp->dnode;
	dnode* pdn = (dnode *)parent->dnode;

	cnode* cn = alloc_cnode(sp->function, sp->argument, sp->level,
				sp->order, sp->flags, sp->name);
	cnpair cnp = make_pair(sp->crc, *cn);

	if (!is_inlist(cnp, pdn->children))
		insert_cnode(pdn->children, cnp);
	else
		delete cn;

	cnode *pcn = alloc_cnode(parent->function, parent->level, parent->name);
	cnpair_t parentcn = make_pair(parent->crc, *pcn);
	if (!is_inlist(parentcn, qn->parents))
		insert_cnode(qn->parents, parentcn);
	else
		delete pcn;
}
#endif
