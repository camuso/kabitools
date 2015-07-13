/* kabi-map.h - multimap class for kabi-parser and kabi-lookup utilities
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

#ifndef KABIMAP_H
#define KABIMAP_H

enum ctlflags {
	CTL_POINTER 	= 1 << 0,
	CTL_ARRAY	= 1 << 1,
	CTL_STRUCT	= 1 << 2,
	CTL_FUNCTION	= 1 << 3,
	CTL_EXPORTED 	= 1 << 4,
	CTL_RETURN	= 1 << 5,
	CTL_ARG		= 1 << 6,
	CTL_NESTED	= 1 << 7,
	CTL_BACKPTR	= 1 << 8,
	CTL_FILE	= 1 << 9,
	CTL_HASLIST	= 1 << 10,
	CTL_ISDUP	= 1 << 11,
};

enum levels {
	LVL_FILE,
	LVL_EXPORTED,
	LVL_ARG,
	LVL_RETURN = LVL_ARG,
	LVL_NESTED,
	LVL_COUNT
};

// Seek directions for qnode searches
//
enum seekdir {
	SK_UP = +1,
	SK_DN = -1
};

typedef unsigned long crc_t;

// This struct is created to pass information from the sparse environment to
// the database environment. It is not serialized.
struct sparm
{
	crc_t crc;	   // crc of this data type
	crc_t function;    // crc of the function under which it appears
	crc_t argument;	   // crc of the arg or ret under which it appears
	int level;	   // level in the hierarchy
	int order;	   // order in which sparse discovered it
	const char *decl;  // declaration from which we derive the crc
	const char *name;  // identifier
	void *symlist;	   // for compound data types with descendant symbols
	void *dnode;	   // pointer to the dnode created for this data type
	void *cnode;	   // pointer to the cnode for this instance of dnode
	enum ctlflags flags;
};

#ifdef __cplusplus

// Forward declarations
class dnode;
class cnode;

/*
 * The diagram below shows the layout of the graph comprised of dnodes
 * and cnodes.
 *
 * dnode characterizes a declaration. Each declaration has only one
 * corresponding dnode.
 *
 * cnode characterizes an instance of that declaration as it occurs in
 * the hierarchy. cnodes are instantiated as siblings of dnodes and
 * point back to the dnode with its crc.
 *
 * crcnode characterizes the children and parents of non scalar dnodes.
 *
 * Scalar data declarations will have only one dnode and one sibling cnode to
 * characterize that instance in the hierarchy.
 *
 * "int foo" and "int bar" will each have their own dnode with one sibling
 * cnode and no children.
 *
 * Non scalar data declarations still only have one dnode, at least one
 * sibling cnode, and at least one child order/crc pair.
 *
 *  public                                   ^  [parent order/crc]
 * dnode map                                 |        |
 *      |                                    +---> [order/cnode]
 *      |                                    |        |
 *      +--> [crc/dnode] sibling cnode map --+  [sibling order/crc]
 *      |        |                           |
 *      |     children                       |
 *      V       map                          |  [parent order/crc]
 *               |                           |        |
 *               +--> [child order/crc]      +---> [order/cnode]
 *               |                           |        |
 *               +--> [child order/crc]      V  [sibling order/crc]
 *               |
 *               V
 */

// Map for the dnodes.
//
typedef std::map<crc_t, dnode> dnodemap;
typedef dnodemap::value_type dnpair;
typedef dnodemap::iterator dniterator;
typedef dnodemap::reverse_iterator dnreviterator;
typedef std::pair<crc_t, dnode*> dnpair_p;

// Map for the cnodes, siblings of dnodes.
// The hash map is composed of a std::pair typed as cnpair
// cnpair.first  - order in which this cnode was discovered
// cnpair.second - cnode
//
typedef std::map<int, cnode> cnodemap;	// <int order, cnode cn>
typedef cnodemap::value_type cnpair;
typedef cnodemap::iterator cniterator;
typedef std::pair<int, cnode*> cnpair_p;

// crcpair matches a crc to the order in which it was discovered.
// first - order
// second - crc
// crcnodemap will track children of dnodes by containing the crc
// of the dnode and the order in which the child appeared. This order
// number can then be used to find the correct cnode sibling from the
// dnode's sibling map of cnodes.
//
typedef std::map<int, crc_t> crcnodemap;
typedef crcnodemap::value_type crcpair;
typedef crcnodemap::iterator crciterator;

///////////////////////////////////////////////////////////////////////////////
//
// cnode is the hierarchical instance of a declaration (dnode).
// The declaration can be of any data type.
//
class cnode
{
public:
	cnode(){}
	cnode(crc_t func, crc_t arg, int level, int order,
	      ctlflags flags, std::string name)
		: function(func), argument(arg), level(level), order(order),
		  flags(flags), name(name) {}

	void insert(cnodemap&, cnpair);
	void insert(cnodemap&, cnpair_p);

	// We want nodes below an argument or return to have that
	// ancestor in common. This assures that when traversing
	// the tree during a lookup sequence, we will find the correct
	// ARG or RETURN for the corresponding symbol being looked-up.
	//
	// Consider the following.
	//
	//	function struct foo *do_something(struct bar *bar_arg)
	//
	// do_something() has return of type struct foo*, and arg of
	// struct bar*.
	//
	// All descendant symbols discovered under these function parameters
	// should lead back to them. It is possible that there are other
	// struct foo in the file, and we want to assure that instances
	// of these symbols always lead back to the correct ARG or RETURN
	// symbol from which they are descended.
	//
	// Also, it is possible that other functions could have struct
	// foo and struct bar as arguments or return values, so the
	// ARG and RETURN symbols must lead back to their respective
	// distinct functions.
	//
	// Exported functions will always have unique crc, because they
	// all occupy the same namespace and must be distinct.
	//
	crc_t function;	// Function at the top
	crc_t argument;	// ARG or RETURN

	// The hierarchical level at which this cnode appears in the
	// tree.
	int level;

	// The order in which it was discovered, in order to properly
	// reconstruct the hierarchy.
	int order;

	enum ctlflags flags;
	std::string name;

	// These fields point back to the parent and sibling dnodes.
	// Pairs made of the order in which the dnode was found and the
	// dnode's crc.
	std::pair<int, crc_t> parent;	// My parent dnode, by way of crc
	std::pair<int, crc_t> sibling;  // My sibling dnode, by way of crc

	void operator =(const cnode& cn);
	bool operator ==(const cnode& cn) const;

	// Boost serialization
	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		if (version){;}
		ar & function & argument &level & order & flags & name
		   & parent & sibling;
	}

private:
};

///////////////////////////////////////////////////////////////////////////////
//
// dnode is a descriptor of a declaration of any data type encountered by
// the sparse semantic parser/tokenizer.
//
// data type is determined by the crc of the declaration.
//
//    - For primitive (scalar) data types, the crc is for the whole declaration,
//      e.g. "int foo"
//      Scalar data types have no siblings and no children. They exist as cnode
//      instances in the children map of a parent dnode. Their own dnodes are
//      discarded after processing, as they are no longer needed.
//
//    - For nonscalar types (structs, unions, arrays) and functions, the dnode
//      maintains a cnode map of its children and siblings.
//
//         - Functions only have one cnode in its sibling map to characterize
//           itself. The children cnode map of a funcion contains its arguments
//           and its return. If an argument or return is nonscalar, it will
//           also have a dnode and a cnode map of its children.
//
//         - struct, union, and array dnodes can have many sibling cnodes in
//           the siblings cnode map. Each sibling has a cnodepair (crc, cnode)
//           for its parent. The children cnode map contains cnodes for all
//           the elements of the dnode (nonscalar datatype or function).
//
class dnode {

public:
	dnode(){}		// constructor
	dnode(std::string decl) : decl(decl) {}

	void insert(dnodemap&, dnpair);
	void insert(dnodemap&, dnpair_p);

	std::string decl;	// data type declaration
	cnodemap siblings;
	crcnodemap children;
	enum ctlflags flags;

	void operator =(const dnode& dn);
	bool operator ==(const dnode& dn) const;

	// Boost serialization
	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		if (version){;}
		ar & decl & siblings & children;
	}
};

// This class serves as a wrapper for the hash map of qnodes.
// Having a class wrapper allows us to add other controls
// easily if needed in the future.
//
class dnodemapclass
{
public:
	dnodemapclass(){}
	dnodemap dnmap;

	template<class Archive>
	void serialize(Archive &ar, const unsigned int version)
	{
		if (version){;}
		ar & dnmap;
	}
};

/*****************************************
** Function Prototypes
*****************************************/

extern dnodemap& kb_get_public_dnodemap();
//extern dnode* qn_lookup_qnode(dnode* dn, crc_t crc, dnseek dir=QN_UP);
extern int kb_read_dnodemap(std::string filename, dnodemap& dnmap);
//extern void kb_write_dnmap_other(std::string& filename, dnodemap& dnmap);

extern "C"
{
#endif

extern struct sparm *kb_new_sparm(struct sparm *parent, enum ctlflags flags);
extern struct sparm *kb_new_firstsparm(char *file);
extern void kb_init_crc(const char *decl, struct sparm *sp, struct sparm *parent);
extern void kb_update_nodes(struct sparm *qn, struct sparm *parent);
extern void kb_insert_nodes(struct sparm *qn);
//extern void delete_qnode(struct qnode *qn);
//extern struct qnode *qn_lookup_crc(unsigned long crc);
extern void kb_add_to_decl(struct sparm *qn, char *decl);
extern void kb_trim_decl(struct sparm *qn);
extern const char *kb_get_decl(struct sparm *qn);
extern bool kb_is_dup(struct sparm *sp);
extern const char *kb_cstrcat(const char *d, const char *s);
extern void kb_write_dnodemap(const char *filename);
extern void kb_restore_dnodemap(char *filename);
//extern bool kb_merge_dnodemap(char *filename);
extern int kb_dump_dnodemap(char *filename);
//extern void kb_dump_dnode(struct dnode *dn);

#ifdef __cplusplus
}
#endif

#endif // KABIMAP_H
