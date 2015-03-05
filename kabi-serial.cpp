
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>

#include "kabi-node.h"
#include "kabi-serial.h"

//using namespace std;

#if 0
namespace boost {
namespace serialization {



void kb_write_qnode(struct qnode *qn)
{
	Cqnode cq();
	cq.qnode;
	ofstream ofs("kabi-list.dat");
	boost::archive::text_oarchive oa(ofs);

	oa << cq;

}

class Cqnode
{
public:
	Cqnode(){}
	Cqnode(qnode *q);
	qnode *qn;
	cnode *cn;
	friend class boost::serialization::access;
	template <class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		if (version){;}
		ar & cn->crc & cn->level & qn->name & qn->typnam
		   & qn->file & qn->flags
		   & qn->parents & qn->children & qn->declist;
	}
};

Cqnode::Cqnode(qnode *q)
{
	cn->crc      = q->cn->crc;
	cn->level    = q->cn->level;
	qn->name     = q->name;
	qn->typnam   = q->typnam;
	qn->file     = q->file;
	qn->flags    = q->flags;
	qn->parents  = q->parents;
	qn->children = q->children;
	qn->declist  = q->declist;
}

class Cqnodelist
{
public:
	Cqnodelist(){}
	std::vector<int>qnodelist;

	friend class boost::serialization::access;
	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & qnodelist;
	}
};

Cqnodelist ql;

void qn_add_qnode(struct qnode *)
{

}

void kb_write_list ()
{
	ofstream ofs("kabi-list.dat");

	{
		boost::archive::text_oarchive oa(ofs);
		oa << ql.qnodelist;
	}
}


} // namespace serialization
} // namespace boost
#endif
