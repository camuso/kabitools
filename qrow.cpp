/* qrow.cpp - simple row for qnodes
 *
 * Copyright (C) 2015  Red Hat Inc.
 * Tony Camuso <tcamuso@redhat.com>
 *
 * This is free software. You can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

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
