#ifndef KABIDUMP_H
#define KABIDUMP_H

#include <map>
#include "kabi-map.h"

class kabidump
{
public:
	kabidump(){}
	kabidump(int argc, char **argv);

private:
	Cqnodemap& m_cqnmap = get_public_cqnmap();
	qnodemap_t& m_qnodes = m_cqnmap.qnmap;
	std::string m_filename = "../kabi-data.dat";
};

#endif // KABIDUMP_H
