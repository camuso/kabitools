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


#ifdef __cplusplus

// This is a hash map used for parents and children of qnodes.
// The hash map is composed of a std::pair typed as cnpair_t
// cnpair_t.first  - crc
// cnpair_t.second - level
//
// In the parent list, the level field contains the level at which the qnode
// appears in the parent's hierarchy.
// In the child list, it's the level at which the child appears in this
// qnode's hierarchy.
// For example, if a qnode appears at level n in the hierarchy, the parent
// in the qnode's parents map will have n in its level field and the child
// in the qnode's children map will have n+1 in its level map.
//
typedef std::multimap<unsigned long, int> cnodemap_t;
typedef cnodemap_t::value_type cnpair_t;	// pair<unsigned long&&, int&&>
typedef cnodemap_t::iterator cniterator_t;

#endif

struct qnode
{	
	// The crc is the key to the map pair. It is only valid until
	// the parser exits. Thereafter, it is not valid as a qnode
	// field, but rather as the the "first" field of an std::pair
	// comprised of the crc and the qnode itself, which is serialized
	// as the "second" field of the std::pair.
	unsigned long crc;

	// level is volatile and subject to change with each instance,
	// even of qnodes having the same crc. It is used only during
	// the discovery cycle of the qnode to be stored in its parents
	// level field, indicating its level in the hierarchy of its
	// immediate parent. The other parents in the qnode parents map
	// may have different numbers in their level field to reflect
	// where the respective instance of this crc/qnode pair appeared
	// in their respective hierarchies.
	int level;

	// These pointers go out of scope at the end of the parser's life
	// and are not valid when the qnode is deserialized. They are
	// provided in the C namespace for the C-based parser to access.
	// They are meaningless when this structure rematerializes after
	// deserialization.
	char *name;
	void *symlist;

	// This is the only field on this side of the c/c++ devide that
	// is valid after the parser exits.
	enum ctlflags flags;

#ifdef __cplusplus

	// This is the c++ side of the qnode. These fields will be updated
	// at the end of the discovery process for this qnode and are
	// valid when the qnode rematerializes after deserialization.
	qnode(){}
	std::string sname;
	std::string sdecl;
	cnodemap_t parents;
	cnodemap_t children;

	// Boost serialization
	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		if (version){;}
		ar & flags & sdecl & sname & parents & children;
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

// This hash map keeps track of duplicates on a file-by-file basis.
// The dup list can be cumulatively built as each file is processed
// depending on the "cumulative" flag state.
//
typedef std::map<unsigned long, qnode> dupmap_t;
typedef dupmap_t::value_type dupair_t;
typedef dupmap_t::iterator dupiterator_t;

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
extern qnode* qn_lookup_crc_other(unsigned long crc, Cqnodemap& cqnmap);
extern bool qn_is_duplist(qnode* qn, qnodemap_t& qnmap);
extern void kb_read_cqnmap(std::string filename, Cqnodemap &cqnmap);
extern void kb_write_cqnmap_other(std::string& filename, Cqnodemap& cqnmap);
extern "C"
{
#endif

extern struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags);
extern struct qnode *new_firstqnode(char *file);
extern void update_qnode(struct qnode *qn, struct qnode *parent);
extern void update_dupmap(struct qnode *qn);
extern void insert_qnode(struct qnode *qn);
extern void delete_qnode(struct qnode *qn);
extern void qn_add_parent(struct qnode *qn, struct qnode *parent);
extern void qn_add_child(struct qnode *qn, struct qnode *child);
extern struct qnode *qn_lookup_crc(unsigned long crc);
extern struct qnode *qn_lookup_crc_slist(unsigned long crc);
extern bool qn_lookup_parent(struct qnode *qn, unsigned long crc);
extern bool qn_lookup_child(struct qnode *qn, unsigned long crc);
extern void qn_add_to_decl(struct qnode *qn, char *decl);
extern void qn_trim_decl(struct qnode *qn);
extern const char *qn_get_decl(struct qnode *qn);
extern bool qn_is_dup(struct qnode *qn, struct qnode* parent);
extern const char *cstrcat(const char *d, const char *s);
extern void kb_write_dupmap(char *filename);
extern void kb_restore_dupmap(char *filename);
extern void kb_write_cqnmap(const char *filename);
extern void kb_restore_cqnmap(char *filename);
extern bool kb_merge_cqnmap(char *filename);
extern void kb_dump_cqnmap(char *filename);
extern void kb_dump_qnode(struct qnode *qn);
#ifdef __cplusplus
}
#endif

#endif // KABIMAP_H
