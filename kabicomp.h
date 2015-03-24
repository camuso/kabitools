/* kabicomp.h - compress a database created by kabi-parser using boost
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
 * Synopsis
 * --------
 * This code assumes that the database being compressed was created by
 * kabi-parser appending new serialized data from multiple files.
 *
 * This presents two problems.
 *
 * 1. Deserialization will only be performed on the data taken from the
 *    very first file, because boost::serialization code reads the number
 *    of records from the first line in the file. In this case, the number
 *    is only for the number of records serialized from each individual file.
 *
 * 2. There will be much duplicated data, since duplicates are only wrung
 *    out for each file, not for all compositely.
 *
 * This utility creates a new composite data base from the multiple
 * serializations that were created one file at a time and compresses the
 * duplicates.
 *
 * The database model being used is a graph. To compress the database, each
 * duplicate vertex must have it edges connected to the original vertex
 * before being deleted as a duplicate.
 *
 * The resulting database created in memory is then serialized to disk.
 */
#ifndef KABICOMP_H
#define KABICOMP_H

#include <sstream>
#include <iostream>
#include <iomanip>
#include "kabi-map.h"

class kabicomp
{
public:
	kabicomp(){}
	kabicomp(std::string& tarfile);

private:
	int shell(std::stringstream& ss_command, std::stringstream& ss_outstr);
	int extract_recordcount(std::string& str);

	Cqnodemap& m_qnmap = get_public_cqnmap();
	qnodemap_t& m_qnodes = m_qnmap.qnmap;

	std::string m_tarfile;
	std::stringstream m_ss_outstr;
	int m_recordcount = 0;
};

#endif // KABICOMP_H
