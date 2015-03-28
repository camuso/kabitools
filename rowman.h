#ifndef ROWMAN_H
#define ROWMAN_H

#include <string>
#include <vector>
#include <map>
#include "kabi-map.h"
#include "qrow.h"

typedef std::vector<qrow> rowvec_t;

class rowman
{
public:
	rowman();
	rowvec_t rows;

	void fill_row(const qnode& qn);
	void put_rows_from_back();
	void put_rows_from_front();

private:
	void print_row(qrow& r, bool verbose = true);
	bool set_dup(qrow& row);
	bool is_dup(qrow& row);
	void clear_dups() {dups.clear(); dups.resize(LVL_COUNT);}

	rowvec_t dups;
	std::string &indent(int padsize);
};


#endif // ROWMAN_H
