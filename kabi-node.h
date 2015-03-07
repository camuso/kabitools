#ifndef KABINODE_H
#define KABINODE_H

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

struct cnode
{
	unsigned long crc;
	int level;
#ifdef __cplusplus
	cnode(){}
	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		ar & crc & level;
	}

#endif
};

struct qnode
{
	struct cnode *cn;
	char *name;
	char *typnam;
	char *file;
	void *symlist;
	enum ctlflags flags;
#ifdef __cplusplus
	qnode(){}
	std::string sname;
	std::string stypnam;
	std::string sfile;
	std::string sdecl;
	std::vector<cnode> parents;
	std::vector<cnode> children;

	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		ar & sname & stypnam & sfile
		   & parents & children & sdecl;
	}
#endif
};

#ifdef __cplusplus

class Cqnodelist
{
public:
	Cqnodelist(){}
	std::vector<qnode> qnodelist;

	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		ar & qnodelist;
	}
};

extern std::vector<qnode> &get_qnodelist();
extern "C"
{
#endif
extern struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags);
extern struct qnode *new_firstqnode(enum ctlflags flags);
extern void update_qnode(struct qnode *qn);
extern void delete_qnode(struct qnode *qn);
extern void qn_add_parent(struct qnode *qn, struct qnode *parent);
extern void qn_add_child(struct qnode *qn, struct qnode *child);
extern struct qnode *qn_lookup_crc(unsigned long crc);
extern bool qn_lookup_parent(struct qnode *qn, unsigned long crc);
extern bool qn_lookup_child(struct qnode *qn, unsigned long crc);
extern void qn_add_to_declist(struct qnode *qn, char *decl);
extern const char *qn_extract_type(struct qnode *qn);
extern bool qn_is_dup(struct qnode *qn, struct qnode* parent, unsigned long crc);
extern const char *cstrcat(const char *d, const char *s);
#ifdef __cplusplus
}
#endif

#endif // KABINODE_H
