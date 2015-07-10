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
static int order = 0;

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
	decl = dn.decl;
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
 * Look into the parents map of a dnode to see if any of the parents
 * there share the same ancestry as the cn parameter.
 */
#if 0
Need to rethink how to do this.
static bool has_same_ancestry(dnode& dn, cnode& cn)
{
	cnodemap& parents = dn.parents;
	cniterator cnit = find_if(parents.begin(), parents.end(),
		[&cn](cnpair lcnp)
		{
			cnode& lcn = lcnp.second;
			return (lcn.function == cn.function);
		});

	return (cnit != parents.end());
}
#endif

static dnpair* lookup_dnode(cnpair cnp)
{
	dnodemap& dnmap = public_dnodemap;
	dnitpair  range = dnmap.equal_range(cnp.first);
	dniterator dnit = range.first;
	int count = distance(range.first, range.second);

	if ((count < 1) || (range.first == dnmap.end()))
		return NULL;

	if (count == 1)
		return &(*dnit);

#if 0
	Need to rethink how to do this.
	dnit = find_if (range.first, range.second,
		[&cnp](dnpair& ldn)
		{
			dnode& rdn = ldn.second;
			cnode& cn = cnp.second;

			return has_same_ancestry(rdn, cn);
		});
#endif

	return dnit != range.second ? &(*dnit) : NULL;
}

/******************************************************************************
 * alloc_sparm
 *
 * This is the object that conveys information from the parser to the c++
 * side of the world.
 */
static inline sparm* alloc_sparm()
{
	sparm *sp = new sparm;
	dnode *dn = new dnode;
	sp->dnode = (void*)dn;	// dnode descriptor of this declaration
	return sp;
}

static inline cnode* alloc_cnode(edgepair& function,
				 edgepair& argument,
				 int level,
				 int order,
				 ctlflags flags,
				 string name)
{
	cnode *cn = new cnode(function, argument, level, order, flags, name);
	return cn;
}

static inline void insert_dnode(dnodemap& dnmap, dnpair dnp)
{
	dnmap.insert(dnmap.end(), dnp);
}

static inline void insert_cnode(cnodemap& cnmap, pair<crc_t, cnode> cn)
{
	cnmap.insert(cnmap.end(), cn);
}

static inline
sparm* init_sparm(sparm* parent, sparm *sp, enum ctlflags flags)
{
	sp->name    = "";
	sp->symlist = NULL;
	sp->flags   = flags;
	sp->level = parent->level+1;
	sp->order = ++order;
	return sp;
}

/******************************************************************************
 * init_crc(const char *decl, qnode *qn, qnode *parent)
 *
 * decl   - declaration of type to be converted to crc
 * sp     - struct sparm containing the details of this instance of this symbol
 * parent - parent sparm
 *
 * See the commentary in kabi-map.h
 *
 * If the parent is an arg or ret, then the argument field will contain the crc
 * of the declaration of the parent's data type.
 *
 * If the parent is NOT an arg or ret, the argument field will contain the crc
 * of the parent's argument field.
 *
 * This logic guarantees that the exported function's argument will be
 * ancestral to all the data types that appear below it in the hierarchy
 * of the exported function. The argument field of the data types appearing
 * first level down from the ancestral argument are the first to appear
 * under that argument, so their argument fields point back to the crc of
 * the exported function's argument itself. Data types at lower levels
 * also point back to the same exported function's argument by inheriting
 * its crc from the upper levels, back to the first level.
 *
 * If the data type is an exported function, its function field will contain
 * the crc of its own declaration. For all data below the function level, the
 * function field will contain the crc of the exported function at the top
 * level of the hierarchy.
 *
 */
void kb_init_crc(const char *decl, struct sparm *sp, struct sparm *parent)
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
 * kb_new_firstsparm(char *file)
 *
 * Returns new first sparm
 *
 * The File is parent of all symbols found within it. Because it is at the
 * top of the hierarchy, it's ancestry fields, function and argument, will
 * have zeroes in it.
 */
struct sparm *kb_new_firstsparm(char *file)
{
	struct sparm *sp= alloc_sparm();

	sp->name = "";
	sp->decl = file;
	sp->level = LVL_FILE;
	sp->order = ++order;
	sp->flags = CTL_FILE;
	sp->crc = raw_crc32(file);
	sp->argument = 0;
	sp->function = 0;

	dnode* dn = (dnode*)sp->dnode;
	dn->decl = sp->decl;
	dn->flags = sp->flags;

	edgepair func = make_pair(sp->function, 0);
	edgepair arg  = make_pair(sp->argument, 0);
	cnode* cn = alloc_cnode(func, arg, sp->level, sp->order,
				sp->flags, sp->name);
	insert_cnode(dn->siblings, make_pair(sp->crc, *cn));
	insert_dnode(public_dnodemap, make_pair(sp->crc, *dn));

	return sp;
}


/****************************************************************************
 * kb_update_nodes
 *
 * Given the sparm of the newly processed node and its parent, update the
 * corresponding dnode and cnode with data collected by kabi::get_symbols
 * and kabi::get_declist.
 * If the dnode is the first of its type, as characterized by its CRC, its
 * cnode will be the first entry in its siblings cnodemap, and the dnode
 * will be inserted into the public_dnodemap.
 * If not, the original dnode will get this dnode's cnode inserted into its
 * siblings cnodemap, and the dnode for this symbol will be dropped on the
 * floor.
 */
void kb_update_nodes(struct sparm *sp, struct sparm *parent)
{
	// Varibles we can assign now

	dnode* dn = (dnode *)sp->dnode;
	dnode* pdn = (dnode *)parent->dnode;
	dnpair dnp = make_pair(sp->crc, *dn);

	cnode* pcn = (cnode *)parent->cnode;
	cnpair pcnp = make_pair(parent->crc, *pcn);

	edgepair func = make_pair(sp->function, LVL_EXPORTED);
	edgepair arg = make_pair(sp->argument, LVL_ARG);

	// Variables we must assign later.

	cnode* cn;
	std::pair<crc_t, cnode> cnp;

	// Extract the declaration string from the one stored in the dnode
	// by calls to kb_add_to_decl() made by kabi.c::get_declist and
	// store it in the sparm.decl field to be passed back to the caller.
	sp->decl = dn->decl.c_str();
	dn->flags = sp->flags;

	// Create a cnode for this declaration and copy its pointer to
	// the sparm to be used if the sparm is also a parent.
	cn = alloc_cnode(func, arg, sp->level, sp->order, sp->flags, sp->name);
	sp->cnode = (void *)cn;

	// Create a cnode pair using the new cnode descriptor of this symbol
	// and its CRC. Insert the cnode into the parent dnode's children
	// cnodemap.
	cnp = make_pair(sp->crc, *cn);
	insert_cnode(pdn->children, cnp);

	// Insert the parent's crc/cnode pair into this cnode's parent map.
	// The map will contain only this cnode's parent crc/cnode pair.
	cn->parent.insert(pcnp);

	// If we've seen this dnode before, use the cnodepair to lookup
	// the original dnode instance and insert the cnode of the new
	// dnode into the sibling cnodemap of the original instance.
	// If thisis the first instance of this dnode instance, then its
	// cnode will be the first in its siblings cnodemap.
	dnpair* sib = (sp->flags & CTL_ISDUP) ? lookup_dnode(cnp) : &dnp;
	insert_cnode(sib->second.siblings, cnp);
	cn->sibling.insert(*sib);

	// If we've seen this dnode before, then we're done at this point.
	// This dnode will not be inserted in the public_dnodemap, since
	// there is already a dnode for this symbol in the public_dnodemap.
	// All the hierarchical details of this node have been stored as a
	// cnode in the dnode's siblings cnodemap.
	if (sp->flags & CTL_ISDUP)
		return;

	// Now insert this unique dnode into the public_dnodemap.
	insert_dnode(public_dnodemap, make_pair(sp->crc, *dn));
#if 0
#endif
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

void kb_add_to_decl(struct sparm *sp, char *decl)
{
	dnode* dn = (dnode *)sp->dnode;
	if (dn->decl.size() != 0)
		dn->decl += " ";
	dn->decl += string(decl);
}

void kb_trim_decl(struct sparm *sp)
{
	dnode* dn = (dnode *)sp->dnode;
	dn->decl.erase(dn->decl.find_last_not_of(' ') + 1);
}

const char *kb_get_decl(struct sparm *sp)
{
	dnode* dn = (dnode *)sp->dnode;
	return dn->decl.c_str();
}

bool kb_is_dup(struct sparm *sp)
{
	dnodemap& dnmap = public_dnodemap;
	dnitpair range = dnmap.equal_range(sp->crc);
	int count = distance(range.first, range.second);

	if ((sp->level < LVL_NESTED) || (count == 0))
		return false;

	if (count > 0)
		return true;

	return false;
}

static inline void write_dnodemap(const char *filename, dnodemap dnmap)
{
	ofstream ofs(filename, ofstream::out | ofstream::app);
	if (!ofs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		exit(1);
	}

	{
		boost::archive::text_oarchive oa(ofs);
		oa << dnmap;
	}
	ofs.close();
}

void kb_write_dnodemap_other(string& filename, dnodemap& dnmap)
{
	write_dnodemap(filename.c_str(), dnmap);
}

void kb_write_dnodemap(const char *filename)
{
	write_dnodemap(filename, public_dnodemap);
}

void kb_restore_dnodemap(char *filename)
{
	ifstream ifs(filename);
	if (!ifs.is_open()) {
		fprintf(stderr, "File %s does not exist. A new file"
				" will be created\n.", filename);
		return;
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> public_dnodemap;
	}
	ifs.close();
}

int kb_read_dnodemap(string filename, dnodemap& dnmap)
{
	ifstream ifs(filename.c_str());
	if (!ifs.is_open()) {
		cout << "Cannot open file: " << filename << endl;
		return(-1);
	}

	{
		boost::archive::text_iarchive ia(ifs);
		ia >> dnmap;
	}
	ifs.close();
	return 0;
}

#include <boost/format.hpp>
using boost::format;


void static inline dump_cnmap(cnodemap& cnmap, const char* name)
{
	if (cnmap.size() == 0)
		return;

	cout << format("\t%s: %3d\n") % name % cnmap.size();

	for (auto i : cnmap) {
		crc_t crc = i.first;
		cnode& cn = i.second;
		// crc level order flags func_crc arg_crc name
		cout << format("\t\t  %12lu %3d %3d %08X %12lu %12lu")
			% crc % cn.level % cn.order % cn.flags
			% cn.function.first % cn.argument.first;
		if (cn.flags & CTL_POINTER)
			cout << " *";
		if (cn.name.size() > 0)
			cout << " " << cn.name;
		cout << endl;
	}
}


int kb_dump_dnodemap(char *filename)
{
	dnodemap& dnmap = public_dnodemap;

	if (int retval = kb_read_dnodemap(string(filename), dnmap) != 0)
		return retval;

	cout << "map size: " << dnmap.size() << endl;

	for (auto it : dnmap) {
		dnpair dnp = it;
		crc_t  crc = dnp.first;
		dnode& dn  = dnp.second;

		if (dn.flags & CTL_FILE)
			cout << "FILE: ";

		cout << format("%12lu %s ") % crc % dn.decl;
		dump_cnmap(dn.siblings, "sibliings");
		dump_cnmap(dn.children, "children");
		cout << endl;
	}
	return 0;
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
