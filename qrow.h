#ifndef QROW_H
#define QROW_H
/* qrow.h - simple row for qnodes
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
	int order;
	int flags;
	std::string file;
	std::string decl;
	std::string name;
};
#endif // QROW_H
