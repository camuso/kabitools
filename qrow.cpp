#include "qrow.h"

qrow::qrow(const qrow& r)
{
	crc = r.crc;
	level = r.level;
	flags = r.flags;
	file = r.file;
	decl = r.decl;
	name = r.name;
	flags = r.flags;
}

void qrow::operator = (const qrow& r)
{
	crc = r.crc;
	level = r.level;
	flags = r.flags;
	file = r.file;
	decl = r.decl;
	name = r.name;
	flags = r.flags;
}

bool qrow::operator ==(const qrow& r) const
{
	return ((crc == r.crc) && (level == r.level) &&
		(decl == r.decl) && (name == r.name) &&
		(flags == r.flags));
}
