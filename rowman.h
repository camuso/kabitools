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
	void put_rows_from_back(bool quiet = false);
	void put_rows_from_front(bool quiet = false);
	void put_rows_from_back_normalized(bool quiet = false);
	void put_rows_from_front_normalized(bool quiet = false);

private:
	std::string &indent(int padsize);
	void print_row(qrow& r, bool quiet = false);
	void print_row_normalized(qrow& r, bool quiet = false);
	bool set_dup(qrow& row);
	bool is_dup(qrow& row);
	void clear_dups() { dups.clear(); dups.resize(LVL_COUNT); }
	void clear_dups(qrow &row);

	rowvec_t dups;
	bool m_normalized = false;
	int m_normalized_level;
};


#endif // ROWMAN_H