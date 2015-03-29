#ifndef QROW_H
#define QROW_H

#include <string>

class qrow {
public:
	qrow(){}
	qrow(int flags) : flags(flags) {}
	qrow(const qrow& r);

	void operator =(const qrow& r);
	bool operator ==(const qrow& r) const;
	void clear();

	unsigned long crc;
	int level;
	int flags;
	std::string file;
	std::string decl;
	std::string name;
};
#endif // QROW_H
