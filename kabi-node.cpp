#include <vector>
#include <cstring>
#include <fstream>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>

#include "kabi-node.h"

using namespace std;

Cqnodelist cq;

qnode *alloc_qnode()
{
	qnode *qn = new qnode;
	cnode *cn = new cnode;
	qn->cn = cn;
	return qn;
}

qnode *init_qnode(qnode *parent, qnode *qn, enum ctlflags flags)
{
	qn->flags = flags;
	qn->cn->level = parent->cn->level + 1;
	qn->parents.push_back(*(parent->cn));
	parent->children.push_back(*(qn->cn));

	cq.qnodelist.push_back(*qn);

	return qn;
}

struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags)
{
	qnode *qn = alloc_qnode();
	init_qnode(parent, qn, flags);
	return qn;
}

struct qnode *new_firstqnode(enum ctlflags flags)
{
	struct qnode *qn = alloc_qnode();
	init_qnode(qn, qn, flags);
	return qn;
}

void get_qnodelist(vector<qnode> &qlist)
{
	qlist = cq.qnodelist;
}
void delete_qnode(struct qnode *qn)
{
	delete qn->cn;
	delete qn;
}

void qn_add_parent(struct qnode *qn, struct qnode *parent)
{
	qn->parents.push_back(*(parent->cn));
}

void qn_add_child(struct qnode *qn, struct qnode *child)
{
	qn->children.push_back(*(child->cn));
}

struct qnode *qn_lookup_crc(unsigned long crc)
{
	for (unsigned i = 0; i < cq.qnodelist.size(); ++i)
		if (cq.qnodelist[i].cn->crc == crc)
			return &(cq.qnodelist[i]);
	return NULL;
}

bool qn_lookup_parent(struct qnode *qn, unsigned long crc)
{
	qnode q = *qn;
	for (unsigned i = 0; i < q.parents.size(); ++i)
		if (q.parents[i].crc == crc)
			return true;
	return false;
}

bool qn_lookup_child(struct qnode *qn, unsigned long crc)
{
	qnode q = *qn;
	for (unsigned i = 0; i < q.children.size(); ++i)
		if (q.children[i].crc == crc)
			return true;
	return false;
}

void qn_add_to_declist(struct qnode *qn, char *decl)
{
	qn->sdecl += string(decl) + string(" ");
}

const char *qn_extract_type(struct qnode *qn)
{
	return qn->sdecl.c_str();
}

bool qn_is_dup(struct qnode *qn, struct qnode* parent, unsigned long crc)
{
	struct qnode *top = qn_lookup_crc(crc);

	if (top) {
		qn_add_parent(top, parent);
		parent->children.pop_back();
		delete_qnode(qn);
		return true;
	}
	return false;
}

const char *cstrcat(const char *d, const char *s)
{
	string dd = string(d) + string(s);
	return dd.c_str();
}

#if 0

class foo
{
public:
	int bar;
	char *name;

	template<class Archive>
        void serialize(Archive &ar, const unsigned int version)
        {
		ar & name;
	}
};

ostream & operator<<(ostream &os, const  char *n)
{
	os << n;
	return os;
}

void write_foo(foo *f)
{
	ofstream ofs("foo.txt");
	{
		boost::archive::text_oarchive oa(ofs);
		oa << f;
	}
}

void kb_write_cnode(cnode *cn)
{
	ofstream ofs("kabi-list.dat");
	{
		boost::archive::text_oarchive oa(ofs);
		oa << cn;
	}
}

void kb_write_qnode(qnode *qn)
{
	ofstream ofs("kabi-list.dat");
	{
		boost::archive::text_oarchive oa(ofs);
		oa << qn;
	}
}


void kb_write_qlist()
{
	ofstream ofs("kabi-list.dat");

	{
		vector<qnode *>qnodelist;
		get_qnodelist(qnodelist);
		boost::archive::text_oarchive oa(ofs);
		oa << cq.qnodelist;
	}
}
#endif
