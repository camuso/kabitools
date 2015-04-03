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
enum qnseek {
	QN_UP = +1,
	QN_DN = -1
};

typedef unsigned long crc_t;

// This struct is created to pass information from the C environment to
// the C++ environment. It is not serialized.
struct qnode
{
	crc_t crc;
	crc_t function;
	crc_t ancestor;
	int level;
	char *decl;
	char *name;
	void *symlist;
	void *dnode;
	enum ctlflags flags;
};

#ifdef __cplusplus

typedef std::pair<crc_t, int> pnode_t;

// cnode will be created in kabi-map.cpp::init_crc(), because that's
// when we will have the crc with which to map this cnode into its
// parents' children map.
//
class cnode
{
public:
	cnode(){}
	cnode(pnode_t& function, pnode_t& ancestor, int level,
	      ctlflags flags, std::string name)
		: function(function), ancestor(ancestor), level(level),
		  flags(flags), name(name)
		{}

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
	pnode_t function;	// Function at the top
	pnode_t ancestor;	// ARG or RETURN

	// The hierarchical level at which this cnode appears in the
	// tree.
	int level;
	ctlflags flags;
	std::string name;

	void operator =(const cnode& cn);
	bool operator ==(const cnode& cn) const;

	// Boost serialization
	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		if (version){;}
		ar & function & ancestor &level & flags & name;
	}

private:
};

// This is a hash map used for children and parents of qnodes.
// The hash map is composed of a std::pair typed as cnpair_t
// cnpair_t.first  - crc
// cnpair_t.second - cnode
//
typedef std::multimap<unsigned long, cnode> cnodemap_t;
typedef cnodemap_t::value_type cnpair_t;
typedef cnodemap_t::iterator cniterator_t;


// There should only be one dnode instance for each data type in every
// file.
class dnode {

public:

	// This is the c++ side of the qnode. These fields will be updated
	// at the end of the discovery process for this instance of the
	// corresponding symbol and are valid when the qnode rematerializes
	// after deserialization.

	dnode(){}		// constructor
	dnode(std::string decl) : decl(decl) {}

	std::string decl;	// data type declaration
	cnodemap_t parents;	// multimap of parent cnodes
	cnodemap_t siblings;	//     :       siblings
	cnodemap_t children;	//     :       children

	void operator =(const dnode& dn);
	bool operator ==(const dnode& dn) const;

	// Boost serialization
	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		if (version){;}
		ar & sdecl & parents & siblings & children;
	}
};


// This is the hash map for the qnodes.
//
typedef std::multimap<crc_t, qnode> dnodemap_t;
typedef dnodemap_t::value_type dnpair_t;
typedef dnodemap_t::iterator dniterator_t;
typedef dnodemap_t::reverse_iterator dnreviterator_t;
typedef std::pair<dniterator_t, dniterator_t> dnitpair_t;

// This class serves as a wrapper for the hash map of qnodes.
// Having a class wrapper allows us to add other controls
// easily if needed in the future.
//
class dnodemap
{
public:
	dnodemap(){}
	dnodemap_t dnmap;

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

extern dnodemap& get_public_cqnmap();
extern dnode* qn_lookup_qnode(dnode* dn, crc_t crc, dnseek dir=QN_UP);
extern int kb_read_dnmap(std::string filename, dnodemap& dnmap);
extern void kb_write_dnmap_other(std::string& filename, dnodemap& dnmap);

extern "C"
{
#endif

extern struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags);
extern struct qnode *new_firstqnode(char *file);
extern void init_crc(const char *decl, struct qnode *qn, struct qnode *parent);
extern void update_qnode(struct qnode *qn, struct qnode *parent);
extern void insert_qnode(struct qnode *qn);
extern void delete_qnode(struct qnode *qn);
extern struct qnode *qn_lookup_crc(unsigned long crc);
extern void qn_add_to_decl(struct qnode *qn, char *decl);
extern void qn_trim_decl(struct qnode *qn);
extern const char *qn_get_decl(struct qnode *qn);
extern bool qn_is_dup(struct qnode *qn, struct qnode *parent);
extern const char *cstrcat(const char *d, const char *s);
extern void kb_write_cqnmap(const char *filename);
extern void kb_restore_cqnmap(char *filename);
extern bool kb_merge_cqnmap(char *filename);
extern int kb_dump_cqnmap(char *filename);
extern void kb_dump_qnode(struct qnode *qn);

#ifdef __cplusplus
}
#endif

#endif // KABIMAP_H
