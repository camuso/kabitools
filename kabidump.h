#ifndef KABIDUMP_H
#define KABIDUMP_H

/* kabidump.h - class to dump .dat file created by kabi-parser
 *
 * Copyright (C) 2015  Red Hat Inc.
 * Tony Camuso <tcamuso@redhat.com>
 *
 * Boost serialization used by kabi-parser does not leave legible text files,
 * even though it is a text archive. There is no '\n' after each record.
 * This utility makes the data legible, and dumps the output to stdout.
 * Output can be redirected to a file by the user.
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
