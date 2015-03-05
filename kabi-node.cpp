#include <vector>
#include <cstring>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>

#include "kabi-node.h"

using namespace std;

struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags)
{
	kgraph *kg = new kgraph;

	if (!parent)
		parent = kg->get_qnode();

	kg->parents.clear();
	kg->children.clear();
	kg->flags = flags;

	kg->cn = cn;
	kg->level = parent->cn->level + 1;
	kg->parents.push_back(parent->cn);
	parent->children.push_back(cn);
	qnodelist.push_back(qn);

	return qn;
}


#if 0
vector<qnode *>::iterator get_qnodelist_iterator()
{
	return qnodelist.begin();
}

void get_qnodelist(vector<qnode *> &qlist)
{
	qlist = qnodelist;
}

struct qnode *new_qnode(struct qnode *parent, enum ctlflags flags)
{
	struct qnode *qn = new qnode;
	struct cnode *cn = new cnode;

	memset(qn, 0, sizeof(qnode));
	memset(cn, 0, sizeof(cnode));

	if (!parent)
		parent = qn;

	qn->parents.clear();
	qn->children.clear();
	qn->flags = flags;

	qn->cn = cn;
	cn->level = parent->cn->level + 1;
	qn->parents.push_back(parent->cn);
	parent->children.push_back(cn);
	qnodelist.push_back(qn);

	return qn;
}
#endif

void delete_qnode(struct qnode *qn)
{
	delete qn->cn;
	delete qn;
}

void qn_add_parent(struct qnode *qn, struct qnode *parent)
{
	qn->parents.push_back(parent->cn);
}

void qn_add_child(struct qnode *qn, struct qnode *child)
{
	qn->children.push_back(child->cn);
}

struct qnode *qn_lookup_crc(unsigned long crc)
{
	vector<qnode *>::iterator i;
	for (i = qnodelist.begin(); i < qnodelist.end(); ++i)
		if ((*i)->cn->crc == crc)
			return *i;
	return NULL;
}

bool qn_lookup_parent(struct qnode *qn, unsigned long crc)
{
	vector<cnode *>::iterator i;
	for ( i = qn->parents.begin(); i < qn->parents.end(); ++i) {
		if ((*i)->crc == crc)
			return true;
	}
	return false;
}

bool qn_lookup_child(struct qnode *qn, unsigned long crc)
{
	vector<cnode *>::iterator i;
	for (i = qn->children.begin(); i < qn->parents.end(); ++i) {
		if ((*i)->crc == crc)
			return true;
	}
	return false;
}

void qn_add_to_declist(struct qnode *qn, char *decl)
{
	qn->declist.push_back(decl);
}

void qn_extract_type(struct qnode *qn, char *sbuf, int len)
{
	memset(sbuf, 0, len);
	vector<char *>::iterator i;
	for(i = qn->declist.begin(); i < qn->declist.end(); ++i) {
		strcat (sbuf, *i);
		strcat (sbuf, " ");
	}
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


