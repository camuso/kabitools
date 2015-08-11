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

/***********************************
**  Class encapsulated functions
***********************************/

void cnode::operator = (const cnode& cn)
{
	function = cn.function;
	argument = cn.argument;
	level = cn.level;
	order = cn.order;
	flags = cn.flags;
	name = cn.name;
	parent = cn.parent;
	sibling = cn.sibling;
}

bool cnode::operator ==(const cnode& cn) const
{
	return ((function == cn.function) && (level == cn.level));
}

void cnode::insert(cnodemap& cnmap, cnpair cnp)
{
	cnmap.insert(cnmap.end(), cnp);
}

void cnode::insert(cnodemap& cnmap, cnpair_p cnp_p)
{
	cnpair cnp = make_pair(cnp_p.first, *cnp_p.second);
	cnmap.insert(cnmap.end(), cnp);
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

void dnode::insert(dnodemap& dnmap, dnpair dnp)
{
	dnmap.insert(dnmap.end(), dnp);
}

void dnode::insert(dnodemap& dnmap, dnpair_p dnp_p)
{
	dnpair dnp = make_pair(dnp_p.first, *dnp_p.second);
	dnmap.insert(dnmap.end(), dnp);
}

/***********************************
**  Static functions
***********************************/

static dnpair* lookup_dnode(crc_t crc)
{
	dniterator dnit = public_dnodemap.find(crc);
	return dnit == public_dnodemap.end() ? NULL : &(*dnit);
}

static inline dnpair* insert_dnode(dnodemap& dnmap, dnpair dnp)
{
	dniterator dnit = dnmap.insert(dnmap.end(), dnp);
	return &(*dnit);
}

static inline cnpair* insert_cnode(cnodemap& cnmap, cnpair cnp)
{
	cniterator cnit = cnmap.insert(cnmap.end(), cnp);
	return &(*cnit);
}

static inline crcpair* insert_crcnode(crcnodemap& crcmap, crcpair crcp)
{
	crciterator crcit = crcmap.insert(crcmap.end(), crcp);
	return &(*crcit);
}

// Use this template function when we don't care about the return value.
template <typename Tmap, typename Tpair>
static void insert_node(Tmap& xnodemap, Tpair xpair)
{
	xnodemap.insert(xnodemap.end(), xpair);
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

static inline cnode* alloc_cnode(crc_t function,
				 crc_t argument,
				 int level,
				 int order,
				 ctlflags flags,
				 string name)
{
	cnode *cn = new cnode(function, argument, level, order, flags, name);
	return cn;
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

/***********************************
**  Global functions
***********************************/

/******************************************************************************
 * kb_is_adjacent(cnode &ref, cnode &dyn, int step)
 *
 * ref - cnode that is the point of reference
 * dyn - cnode that has been selected to be compared, usually from a cnodemap
 *       in a loop.
 * step - the direction of the comparison.
 *
 * Determine whether the ref cnode has the same ancestry and is the correct
 * level up or down (parent or child) from the dyn cnode.
 *
 * Seeking a parent
 *	ref <- child cnode
 *	dyn <- parent cnode
 *	step <- SK_PARENT
 *
 * Seeking a child
 *	ref <- parent cnode
 *	dyn <- child cnode
 *	step <- SK_CHILD
 */
bool kb_is_adjacent(cnode& ref, cnode& dyn, seekdir step)
{
	int nextlevel = ref.level + step;

	switch (ref.level) {
	case LVL_FILE :
		return true;
	case LVL_EXPORTED :
		return (dyn.level == nextlevel);
	case LVL_ARG :
		return ((dyn.level == nextlevel) &&
			(dyn.function == ref.function));
	default :
		return ((dyn.level == nextlevel) &&
			(dyn.function == ref.function) &&
			(dyn.argument == ref.argument));
	}
	return false;
}

/******************************************************************************
 * kb_init_crc(const char *decl, qnode *qn, qnode *parent)
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
void kb_init_crc(const char* string, struct sparm *sp, struct sparm *parent)
{
	sp->crc = raw_crc32(string);

	if (sp->flags & CTL_ANON) {
		std::string anon = to_string(sp->order);
		sp->crc = crc32(anon.c_str(), parent->crc);
	}

#ifndef NDEBUG
	if ((sp->crc == 2674120813))// || (parent->crc == 410729264))
		puts(decl);
#endif

	if (sp->flags & (CTL_ARG | CTL_RETURN))
		sp->argument = sp->crc;
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
	cnode *cn;
	dnode *dn;
	cnpair* cnp;
	dnpair* dnp;

	sp->name = "";
	sp->decl = file;
	sp->level = LVL_FILE;
	sp->order = ++order;
	sp->flags = CTL_FILE;
	sp->crc = raw_crc32(file);
	sp->argument = 0;
	sp->function = 0;

	dn = (dnode*)sp->dnode;
	dn->decl = sp->decl;

	crc_t func = sp->function;
	crc_t arg  = sp->argument;
	cn = alloc_cnode(func, arg, sp->level, sp->order, sp->flags, sp->name);
	cn->sibling = make_pair(sp->order, sp->crc);
	cn->parent = make_pair(0,0);
	sp->cnode = (void *)cn;

	cnp = insert_cnode(dn->siblings, make_pair(sp->order, *cn));
	dnp = insert_dnode(public_dnodemap, make_pair(sp->crc, *dn));

	sp->cnode = (void *)&cnp->second;
	sp->dnode = (void *)&dnp->second;

	return sp;
}

/****************************************************************************
 * kb_update_nodes
 *
 * This is the heart of the graph and establishes the edges (cnodes and
 * crcnodes) to each vertex (dnode). Consequently, there's a lot of
 * 'splainin' to do.
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
 *
 * NOTE:
 *	make_pair creates a pair on the stack, so it is not persistent.
 *
 * Create a new cnode (edge) for this dnode (vertex).
 *	func
 *	arg
 *	level
 *	order
 *	flags
 *	name
 *
 * Into the new cnode ...
 *	Put the order/crc pair of the parent dnode in the new cnode parent field
 *	Put the order/crc pair of the sibling dnode in the new cnode sibling
 *	field
 *		Must first determine where the sibling dnode is. Do a lookup
 *		using the crc. If a dnode is found, it means this is a dup
 *		or backpointer, so use the found dnode as the sibling.
 *		Duplicate dnodes will be dropped, but backpointer dnodes
 *		will be kept to a depth of one.
 *	cnode is complete at this time
 * Into the sibling dnode
 *	Put this new cnode into the sibling dnode's siblings cnodemap
 * Into the parent dnode
 *	Put the order/crc pair of this dnode into its parent's crcnodemap
 *	of its children.
 */
void kb_update_nodes(struct sparm *sp, struct sparm *parent)
{
	// Varibles we can assign now
	dnode* dn = (dnode *)sp->dnode;		// ptr to this dnode
	dnpair dnp = make_pair(sp->crc, *dn);	// this dnode pair <crc,dnode>
	dnode* pdn = (dnode *)parent->dnode;	// ptr to parent dnode
	crc_t func = sp->function;		// ancestry crc's
	crc_t arg = sp->argument;		// :

	// Variables we must assign later.
	cnode* cn;	// ptr to cnode for this dnode
	cnpair* cnp;	// ptr to cnode pair <order,cnode> for this dnode
	dnpair* sib;	// ptr to first sibling dnode pair <crc,dnode>
	cnode* sibcn;	// ptr to first sibling cnode in dnode's cnodemap

	// Extract the declaration string from the one stored in the dnode
	// by kabi.c::get_declist and store it in the sparm.decl field to
	// be passed back to the caller through this sparm,
	sp->decl = dn->decl.c_str();

	// Create a cnode for this declaration.
	cn = alloc_cnode(func, arg, sp->level, sp->order, sp->flags, sp->name);
	cn->parent = make_pair(parent->order, parent->crc);

	// If we've seen this dnode before, use the cnodepair to lookup
	// the original dnode instance and insert the cnode of the new
	// dnode into the sibling cnodemap of the original instance.
	// If this is the first instance of this dnode, then its cnode
	// will be the first in its siblings cnodemap.
	// Either way, its sibling field will point to the first sibling
	// in the sibling cnodemap, which is the sibling belonging to the
	// original instance of this declaration/symbol.
	sib = lookup_dnode(sp->crc);
	sib = sib ? sib : &dnp;

	// If we haven't created the dnode's siblings cnodemap yet, it's
	// because this is the first of its kind. Therefore, the first
	// sib in the dnode's siblings cnodemap will be this dnode's cnode.
	sibcn = sib->second.siblings.size() > 0 ?
		&(sib->second.siblings.begin()->second) : cn;

	cn->sibling = make_pair(sibcn->order, sib->first);
	cnp = insert_cnode(sib->second.siblings, make_pair(sp->order, *cn));
	sp->cnode = (void *)&cnp->second;

	// If this cnode is one level up from its parent's cnode, and shares
	// the same ancestry as the parent's cnode, then we can insert it
	// into the parent's children cnodemap. Parent's cnode is the first
	// one in the parent dnode's sibling cnodemap.
	if (kb_is_adjacent(pdn->siblings.begin()->second, *cn, SK_CHILD))
		insert_node(pdn->children, make_pair(sp->order, sp->crc));

	// If this dnode is a dup or a backpointer, then return without
	// inserting the dnode into the public_dnodemap, because there's
	// already one there.
	// All the hierarchical details of this node have been stored as a
	// cnode in the original dnode's siblings cnodemap.
	if (sp->flags & CTL_ISDUP)
		return;

	sib = insert_dnode(public_dnodemap, dnp);
	sp->dnode = (void *)&sib->second;
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

dnode* kb_lookup_dnode(crc_t crc)
{
	dnpair* dnp = lookup_dnode(crc);
	if (!dnp)
		return NULL;
	return &dnp->second;
}

bool kb_is_dup(struct sparm *sp)
{
	dnodemap& dnmap = public_dnodemap;

	if (sp->level <= LVL_ARG)
		return false;

	dniterator dnit = dnmap.find(sp->crc);
	bool dup = dnit != dnmap.end() ? true : false;
	return dup;
}

/*******************************************
**  Serialization and Extraction functions
*******************************************/

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


void static inline dump_cnmap(cnodemap& cnmap, const char* field)
{
	if (cnmap.size() == 0)
		return;

	cout << format("\n\t%s: %3d\n") % field % cnmap.size();

	// func arg level order flags par_order par_crc sib_order sib_crc name
	for (auto i : cnmap) {
		int order = i.first;
		cnode& cn = i.second;

		cout << format("\t%12lu %12lu %3d %5d %04X %5d %12lu %5d %12lu ")
			% cn.function % cn.argument
			% cn.level % order % cn.flags
			% cn.parent.first % cn.parent.second
			% cn.sibling.first % cn.sibling.second;
		if (cn.flags & CTL_POINTER)
			cout << "*";
		if (cn.name.size() > 0)
			cout << cn.name;
		if (cn.flags & CTL_FILE)
			cout << " : FILE";
		if (cn.flags & CTL_EXPORTED)
			cout << " : EXPORTED";

		cout << endl;
	}
}

void static inline dump_children(dnpair dnp)
{
	crcnodemap crcmap = dnp.second.children;
	cout << format("\n\tchildren: %3d\n") %crcmap.size();

	if (crcmap.size() == 0)
		return;

	for (auto i : crcmap) {
		int order = i.first;
		crc_t crc = i.second;
		dnpair* dnp_p = lookup_dnode(crc);
		dnode dn = dnp_p->second;
		cnodemap siblings = dn.siblings;
		cnode cn = siblings[order];

		cout << format("\t%12lu %5d %s ")
			% crc % i.first % dnp_p->second.decl;

		if (cn.flags & CTL_POINTER)
			cout << "*";
		if (cn.name.size() > 0)
			cout << cn.name;

		if (cn.flags & CTL_FILE)
			cout << " : FILE";

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

		cout << format("%12lu %s ") % crc % dn.decl;
		dump_cnmap(dn.siblings, "siblings");
		dump_children(dnp);
		cout << endl;
	}
	return 0;
}
