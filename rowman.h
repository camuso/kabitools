#ifndef ROWMAN_H
#define ROWMAN_H

#include <map>

enum rowflags {
	ROW_NODUPS	= (1 << 0),
	ROW_ISDUP	= (1 << 1),
	ROW_DONE	= (1 << 2),
};



namespace ROW {

enum levels {
	LVL_FILE,
	LVL_EXPORTED,
	LVL_ARG,
	LVL_RETURN = LVL_ARG,
	LVL_NESTED,
	LVL_COUNT
};

struct row {
	int level;
	int flags;
	std::string file;
	std::string decl;
	std::string name;
	enum rowflags rowflags;
};

}

// Multimap for storing rows ordered by hierarchical level
typedef std::multimap<int, ROW::row> rowmap_t;
typedef rowmap_t::value_type rmpair_t;
typedef rowmap_t::iterator rmiterator_t;
typedef std::pair<rmiterator_t, rmiterator_t> rmitpair_t;


class rowman
{
public:
	rowman();

private:
	rowmap_t rowmap;
	rowmap_t dupmap;

};


#endif // ROWMAN_H
