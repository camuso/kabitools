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

#if 0
struct cnode {
	unsigned long crc;
	int level;
};

struct qnode {
	struct cnode *cn;
	char *name;
	char *typnam;
	char *file;
	enum ctlflags flags;
#ifdef __cplusplus
	std::vector<cnode *> parents;
	std::vector<cnode *> children;
	std::vector<char *> declist;
#endif
};
#endif

#ifdef __cplusplus

class cnode
{
public:
	cnode(){}
	unsigned long crc;
	int level;
};

class qnode
{
public:
	qnode(){}
	unsigned long crc;
	char *name;
	char *typnam;
	char *file;
	enum ctlflags flags;
};

class kgraph
{
public:
	std::vector<cnode *> parents;
	std::vector<cnode *> children;
	std::vector<char *> declist;

	qnode *get_qnode(){return &qn;}
	cnode *get_cnode(){return &cn;}

private:
	qnode qn;
	cnode cn;
	friend class boost::serialization::access;
	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		ar & qn.crc & qn.file & qn.flags &qn.name & qn.typnam
		   & cn.crc & cn.level
		   & parents & children & declist;
        }
};

class std::vector<kgraph> kgraphlist;

extern std::vector<qnode *>::iterator get_qnodelist_iterator();
extern void get_qnodelist(std::vector<qnode *> &qlist);
extern "C"
{
#endif
extern struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags);
extern void delete_qnode(struct qnode *qn);
extern void qn_add_parent(struct qnode *qn, struct qnode *parent);
extern void qn_add_child(struct qnode *qn, struct qnode *child);
extern struct qnode *qn_lookup_crc(unsigned long crc);
extern bool qn_lookup_parent(struct qnode *qn, unsigned long crc);
extern bool qn_lookup_child(struct qnode *qn, unsigned long crc);
extern void qn_add_to_declist(struct qnode *qn, char *decl);
extern void qn_extract_type(struct qnode *qn, char *sbuf, int len);
extern bool qn_is_dup(struct qnode *qn, struct qnode* parent, unsigned long crc);
#ifdef __cplusplus
}
#endif

#endif // KABINODE_H
