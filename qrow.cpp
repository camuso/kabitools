#include "qrow.h"

qrow::qrow(const qrow& r)
{
	crc = r.crc;
	level = r.level;
	flags = r.flags;
	file = r.file;
	decl = r.decl;
	name = r.name;
}

void qrow::operator = (const qrow& r)
{
	crc = r.crc;
	level = r.level;
	flags = r.flags;
	file = r.file;
	decl = r.decl;
	name = r.name;
}

bool qrow::operator ==(const qrow& r) const
{
	return ((crc == r.crc) && (level == r.level) &&
		(decl == r.decl) && (name == r.name));
}

void qrow::clear()
{
	this->crc = 0;
	this->level = 0;
	this->flags = 0;
	this->file = "";
	this->decl = "";
	this->name = "";
}
