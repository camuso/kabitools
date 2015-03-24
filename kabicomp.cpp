/* kabicomp.cpp - compile a database created by kabi-parser using boost
 *
 * The database is fragmented, because there was a different serialization
 * performed for each file processed. This utility creates one contiguous
 * database from the multiple databases in the cumulative serializaton file.
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

#define __cplusplus 201103L
#include <vector>
#include <map>
#include <cstring>
#include <fstream>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include "kabicomp.h"

using namespace std;
/**************************************************************
 * kbcomp::shell - execute a shell command and capture its output
 *
 * command - stringstream containing the command string
 * outstr  - reference to a stringstream that will contain the
 *           output from the shell after running the command
 * BUFSIZ  - defined by the compiler. In g++ running on Linux
 *           64-bit kernel, it's 8192.
 */
int kabicomp::shell(stringstream& command, stringstream& outstr)
{
    char buff[BUFSIZ];
    FILE *fp = popen(command.str().c_str(), "r");

    if (!fp) {
	    cout << "Cannot open file: " << m_tarfile << endl;
	    exit(1);
    }

    while (fgets(buff, BUFSIZ, fp ) != NULL)
        outstr << buff;

    pclose(fp);
    return 0;
}

kabicomp::kabicomp(string &tarfile)
{
	m_tarfile = tarfile;
	stringstream cmd;
	cmd << "tar -tf " << tarfile;
	shell(cmd, m_ss_outstr);
}

int main(int argc, char** argv)
{
	if (argc <= 1) {
		cout << "\nPlease provide the name of the tarfile to unpack."
		     << endl;
		return 1;
	}

	string tarfile = string(argv[1]);
	kabicomp kbc(tarfile);

	return 0;
}
