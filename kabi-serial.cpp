#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>

#include "kabi-serial.h"

using namespace std;

struct cnode {
	unsigned long crc;
	int level;
};

struct qnode {
	struct cnode *cn;
	vector<cnode *> parents;
	vector<cnode *> children;
	vector<char *> *declist;
	char *name;
	char *typnam;
	enum ctlflags flags;
};

qnode *new_qnode(qnode *parent, enum ctlflags flags)
{
	struct qnode *qn = new qnode;
	struct cnode *cn = new cnode;

	memset(qn, 0, sizeof(qnode));
	memset(cn, 0, sizeof(cnode));

	qn->parents.clear();
	qn->children.clear();
	qn->flags = flags;

	qn->cn = cn;
	cn->level = parent->cn->level + 1;
	qn->parents.push_back(parent->cn);
	parent->children.push_back(cn);

	return qn;
}

void qn_add_parent(struct qnode *qn, struct qnode *parent)
{
	qn->parents.push_back(parent->cn);
}

void qn_add_child(struct qnode *qn, struct qnode *child)
{
	qn->children.push_back(child->cn);
}

bool qn_seek_parent(unsigned long crc, qnode *qn)
{
	vector<cnode *>::iterator i;
	for ( i = qn->parents.begin(); i < qn->parents.end(); ++i) {
		if ((*i)->crc == crc)
			return true;
	}
	return false;
}

bool qn_seek_child(unsigned long crc, qnode *qn)
{
	vector<cnode *>::iterator i;
	for (i = qn->children.begin(); i < qn->parents.end(); ++i) {
		if ((*i)->crc == crc)
			return true;
	}
	return false;
}
