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
};

enum levels {
	LVL_FILE,
	LVL_EXPORTED,
	LVL_ARG,
	LVL_RETURN = LVL_ARG,
	LVL_NESTED,
	LVL_COUNT
};

#ifdef __cplusplus

// This is a hash map used for children of qnodes.
// The hash map is composed of a std::pair typed as cnpair_t
// cnpair_t.first  - crc
// cnpair_t.second - level
//
// In the child map, the level indicates wehere the child appears in this
// qnode's hierarchy.
//
// For example, if a qnode appears at level n in the hierarchy, the qnode's
// entry in its parent's children map will have n in its level field.
// Chldren pairs in the qnode's children map will have n+1 in their level
// field.
//
typedef std::multimap<unsigned long, int> cnodemap_t;
typedef cnodemap_t::value_type cnpair_t;	// pair<unsigned long&&, int&&>
typedef cnodemap_t::iterator cniterator_t;

typedef std::pair<unsigned long, int> pnode_t;

#endif

struct qnode
{	
	// The crc is the key to the map pair. It is only valid until
	// the parser exits. Thereafter, it is not valid as a qnode
	// field, but rather as the the "first" field of a std::pair
	// comprised of the crc and the qnode itself, which is serialized
	// as the "second" field of the std::pair.
	unsigned long crc;

	// level in the nested hierarchy where this qnode was instantiated.
	// preserved by serialization.
	int level;

	// These pointers go out of scope at the end of the parser's life
	// and are not valid when the qnode is deserialized. They are
	// provided in the C namespace for the C-based parser to access.
	// They are meaningless when this structure rematerializes after
	// deserialization.
	char *name;
	void *symlist;

	// Flags set during discovery are serialized
	enum ctlflags flags;

#ifdef __cplusplus

	// This is the c++ side of the qnode. These fields will be updated
	// at the end of the discovery process for this qnode and are
	// valid when the qnode rematerializes after deserialization.

	qnode(){}		// constructor
	std::string sname;	// identifier
	std::string sdecl;	// data type declaration
	cnodemap_t children;	// map of children symbol CRCs and their
				// : respective levels in the hierarchy

	// We want nodes below an argument or return to have that
	// ancestor in common. This assures that when traversing
	// the tree in reverse during a lookup sequence, we will find
	// the correct ARG or RETURN for the corresponding symbol being
	// looked-up.
	//
	// Consider the following.
	//
	//	function struct foo *do_something(struct bar *bar_arg)
	//
	// do_something() has return of type struct foo*, and arg of
	// struct bar*.
	//
	// All descendants discovered under these function parameters
	// should lead back to them. It is possible that there are other
	// struct foo in the file, and we want to assure that instances
	// of these structs always lead back to the correct ARG or RETURN
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
	pnode_t parent;		// Immediate predecessor
	pnode_t ancestor;	// ARG or RETURN
	pnode_t function;	// Function at the top

	// Boost serialization
	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		if (version){;}
		ar & flags & level & sdecl & sname
		   & parent & ancestor & function & children;
	}
#endif
};

#ifdef __cplusplus

// This is the hash map for the qnodes.
//
typedef std::multimap<unsigned long, qnode> qnodemap_t;
typedef qnodemap_t::value_type qnpair_t;
typedef qnodemap_t::iterator qniterator_t;
typedef qnodemap_t::reverse_iterator qnriterator_t;
typedef std::pair<qniterator_t, qniterator_t> qnitpair_t;

// This class serves as a wrapper for the hash map of qnodes.
// Having a cass wrapper allows us to add other controls
// easily if needed in the future.
//
class Cqnodemap
{
public:
	Cqnodemap(){}
	qnodemap_t qnmap;

	template<class Archive>
	void serialize(Archive &ar, const unsigned int version)
	{
		if (version){;}
		ar & qnmap;
	}
};

extern Cqnodemap& get_public_cqnmap();
extern qnode *qn_lookup_parent(qnode *qn, unsigned long crc);
extern int kb_read_cqnmap(std::string filename, Cqnodemap &cqnmap);
extern void kb_write_cqnmap_other(std::string& filename, Cqnodemap& cqnmap);

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
extern bool qn_lookup_child(struct qnode *qn, unsigned long crc);
extern void qn_add_to_decl(struct qnode *qn, char *decl);
extern void qn_trim_decl(struct qnode *qn);
extern const char *qn_get_decl(struct qnode *qn);
extern int qn_is_dup(struct qnode *qn);
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
