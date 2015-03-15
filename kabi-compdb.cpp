/* kabi-compdb.cpp - compress a database created by kabi-parser using boost
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

#include <vector>
#include <cstring>
#include <fstream>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include "kabi-node.h"
#include "kabi-compdb.h"

using namespace std;

void kabicompdb::compress()
{
	m_qlist.clear();

	vector<qnode>::iterator it;
	for (it = m_qstore.begin(); it < m_qstore.end(); ++it) {
		qnode *qn = &(*it);
		if (qn_is_duplist(qn)) {

		}
	}
}

int kabicompdb::extract_recordcount(string &str)
{
	using namespace boost;	// tokenizer and lexical_cast
	vector<string> strvec;

	typedef tokenizer<char_separator<char>> tokenizer;
	char_separator<char> sep(" ");
	tokenizer tok(str, sep);
	for (tokenizer::iterator it = tok.begin(); it != tok.end(); ++it) {
		strvec.push_back(*it);
	}
	return lexical_cast<int>(strvec[BA_RECDCOUNT]);
}

void kabicompdb::load_database()
{
	string in;
	streamoff pos ;
	ifstream ifs(m_filename);
	ofstream ofs(m_tempfile, ofstream::out | ofstream::trunc);

	getline(ifs, in);
	ofs << in << endl;

	while (getline(ifs, in)) {
		ofs << in << endl;
		if (in.find("serialization::archive") != string::npos) {
			ofs.close();
			kb_read_qlist(m_tempfile, m_qnlist);
			m_qstore.insert(m_qstore.end(),
					m_qlist.begin(), m_qlist.end());
			ifs.seekg(pos, ifs.beg);
			ofs.open(m_tempfile, ofstream::out | ofstream::trunc);
			getline(ifs, in);
			ofs << in << endl;
		}
		pos = ifs.tellg();
	}

	ifs.close();
	ofs.close();
	kb_read_qlist(m_tempfile, m_qnlist);
	m_qstore.insert(m_qstore.end(), m_qlist.begin(), m_qlist.end());
	remove(m_tempfile.c_str());
}

kabicompdb::kabicompdb(string& filename)
{
	m_filename = filename;
	return;
}

int main(int argc, char** argv)
{
	if (argc <=1) {
		cout << "\nPlease provide the name of the boost::serialize"
			" data file to compress.\n\n";
		exit(1);
	}
	string filename = string(argv[1]);
	kabicompdb kbc(filename);
	kbc.load_database();
	kbc.compress();

	return 0;
}
