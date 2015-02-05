/* kabi.c
 *
 * Search the database file to match the user input. If a match is
 * found, and it is nested, search all the way back to the first
 * ancestor in the chain.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm-generic/errno-base.h>
#define NDEBUG	// comment out to enable asserts
#include <assert.h>
#include <sqlite3.h>

#if !defined(NDEBUG)
#define DBG(x) x
#define RUN(x)
#else
#define DBG(x)
#define RUN(x) x
#endif

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifdef true
#undef true
#endif
#ifdef false
#undef false
#endif
#define true 1
#define false 0

typedef unsigned int bool;

/*****************************************************
** Global declarations
******************************************************/

static const char *helptext ="\
\n\
kabilookup [options] declstr datafile\n\
\n\
    Searches a kabitree database to lookup declaration strings \n\
    containing the user's input. The search is continued until all \n\
    ancestors (containers) of the declaration are found. The results \n\
    of the search are printed to stdout andindented hierarchically.\n\
\n\
    declstr   - Declaration to lookup. Use double quotes \"\" around \n\
    compound strings. \n\
    datafile  - Database file containing the tree of exported functions \n\
                and any and all symbols explicitly or implicitly affected. \n\
                This file should be created by the kabi-data.sh tool. \n\
Options:\n\
              Limits on the number of records displayed: \n\
    -0        no limits \n\
    -1        10 \n\
    -2        100 \n\
    -3        1000 \n\
    -w        whole words only, default is \"match any and all\" \n\
    -i        ignore case \n\
    -v        verbose (default): lists all ancestors \n\
    -h        this help message.\n\
\n";

static bool kb_verbose = true;
static bool kb_whole_word = false;
static int kb_limit = 0;


/*****************************************************
** global sqlite3 data
******************************************************/
static struct sqlite3 *db = NULL;
static const char *table = "kabitree";

// schema enumeration
enum schema {
	COL_ID,
	COL_PARENTID,
	COL_LEVEL,
	COL_FLAGS,
	COL_PREFIX,
	COL_DECL,
	COL_PARENTDECL,
};

/*****************************************************
** View Management
******************************************************/

#define INTSIZ 32
#define PFXSIZ 16
#define DECLSIZ 256

struct row {
	char id[INTSIZ];	// Returned by sqlite as hex char strings
	char parentid[INTSIZ];	//	:
	char level[INTSIZ];	//	:
	char flags[INTSIZ];	//	:
	char prefix[PFXSIZ];
	char decl[DECLSIZ];
	char parentdecl[DECLSIZ];
};

struct row *new_row()
{
	struct row *pr = malloc(sizeof(struct row));
	memset(pr, 0, sizeof(struct row));
	return pr;
}

void delete_row(struct row *pr)
{
	free(pr);
}

struct table {
	int tablesize;
	int rowcount;
	int arraysize;
	union {
		struct row *rows;
		char *array;
	};
};

struct table *new_table(int rowcount)
{
	struct table *tptr = malloc(sizeof(table));
	tptr->arraysize = rowcount * sizeof(struct row);
	tptr->rowcount = rowcount;
	tptr->array = malloc(tptr->arraysize);
	tptr->tablesize = sizeof(struct table) + tptr->arraysize;
	memset(tptr->array, 0, tptr->arraysize);
	return tptr;
}

void delete_table(struct table *tptr)
{
	free(tptr->rows);
	free(tptr);
}

static void copy_row(struct row *drow, struct row *srow)
{
	strncpy(drow->id,         srow->id,         INTSIZ-1);
	strncpy(drow->parentid,   srow->parentid,   INTSIZ-1);
	strncpy(drow->level,      srow->level,      INTSIZ-1);
	strncpy(drow->flags,      srow->flags,      INTSIZ-1);
	strncpy(drow->prefix,     srow->prefix,     PFXSIZ-1);
	strncpy(drow->decl,       srow->decl,       DECLSIZ-1);
	strncpy(drow->parentdecl, srow->parentdecl, DECLSIZ-1);
}

/*****************************************************
** SQLite utilities and wrappers
******************************************************/
// if this were c++, db and table would be private members
// of a sql class, accessible only by member function calls.

static const char *kabitable = "kabitree";

inline struct sqlite3 *sql_get_db()
{
	return db;
}

inline void sql_set_kabitable(const char *tablename)
{
	kabitable = (char *)tablename;
}

inline const char *sql_get_kabitable()
{
	return (const char *)kabitable;
}

bool sql_exec(const char *stmt,
	      int (*cb)(void *, int, char**, char**),
	      void *arg)
{
	char *errmsg;
	int ret = sqlite3_exec(db, stmt, cb, arg, &errmsg);

	if (ret != SQLITE_OK) {
		fprintf(stderr,
			"\nsql_exec: Error in statement: %s [%s].\n",
			stmt, errmsg);
		sqlite3_free(errmsg);
		return false;
	}
	sqlite3_free(errmsg);
	return true;
}

int sql_process_field(void *output, int argc, char **argv, char **colnames)
{
	if (argc != 1)
		return false;
	strcpy(output, argv[0]);
	return 0;
}

// sql_process_row - callback to process one row from a select call
//
// NOTE: the "output" parameter, though cast as void * for the SQLite API,
//       must have been orignially created as struct row *, preferably
//       with a call to new_row().
//
int sql_process_row(void *output, int argc, char **argv, char **colnames)
{
	int i;
	struct row *pr = (struct row *)output;

	for (i = 0; i < argc; ++i) {
		DBG(printf("%s ", argv[i]);)

		switch (i){
		case COL_ID	    :
			strncpy(pr->id, argv[i], INTSIZ-1);
			break;
		case COL_PARENTID   :
			strncpy(pr->parentid, argv[i], INTSIZ-1);
			break;
		case COL_LEVEL	    :
			strncpy(pr->level, argv[i], INTSIZ-1);
			break;
		case COL_FLAGS	    :
			strncpy(pr->flags, argv[i], INTSIZ-1);
			break;
		case COL_PREFIX	    :
			strncpy(pr->prefix, argv[i], PFXSIZ-1);
			break;
		case COL_DECL	    :
			strncpy(pr->decl, argv[i], DECLSIZ-1);
			break;
		case COL_PARENTDECL :
			strncpy(pr->parentdecl, argv[i], DECLSIZ-1);
			break;
		default: return -1;
		}

	}
	DBG(putchar('\n');)

	return 0;
}

// sql_get_one_row - extract one row from a view, given the offset into the view
//
// NOTE: the struct row * must have been orignially created elsewhere,
//       preferably with a call to new_row().
//
bool sql_get_one_row(char *viewname, int offset, struct row *retrow)
{
	bool rval;
	char *zsql = sqlite3_mprintf("select * from %q limit 1 offset %d",
				     viewname, offset);
	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_process_row, (void *)retrow);
	sqlite3_free(zsql);
	return rval;
}

bool sql_get_rows_on_decl(char *viewname, char *declstr, char *output)
{
	bool rval;
	char *zsql;

	if (kb_whole_word)
		zsql = sqlite3_mprintf("select distinct * from %q where "
				       "decl == \'%q\'",
				       viewname, declstr);
	else
		zsql = sqlite3_mprintf("select distinct * from %q where "
				       "decl like \'%%%q%%\'",
				       viewname, declstr);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_process_row, (void *)output);
	sqlite3_free(zsql);
	return rval;
}

bool sql_get_rows_on_id(char *viewname, char *idstr, char *output)
{
	bool rval;
	char *zsql = sqlite3_mprintf("select distinct * from %q "
				     "where id == '%q'",
				     viewname, idstr);
	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_process_row, (void *)output);
	sqlite3_free(zsql);

	return rval;
}

bool sql_create_view_on_decl(char *viewname, char *declstr)
{
	bool rval;
	char *zsql;

	if (! kb_limit)
		zsql = sqlite3_mprintf
				("create temp view %q as select distinct "
				 "* from %q where decl like \'%%%q%%\'"
				 "and level > 2",
				 viewname, sql_get_kabitable(), declstr);
	else
		zsql = sqlite3_mprintf
				("create temp view %q as select distinct "
				 "* from %q where decl like \'%%%q%%\'"
				 "and level > 2 limit %d",
				 viewname, sql_get_kabitable(), declstr,
				 kb_limit);
	DBG(puts(zsql);)
	rval = sql_exec(zsql, 0, 0);
	sqlite3_free(zsql);
	return rval;
}

// sql_process_row_count - callback to process the return for a count request
//
// NOTE: The count comes back as a string (char *) representation of a
//       decimal number.
//
int sql_process_row_count(void *output, int argc, char **argv, char **colnames)
{
	if ((argc != 1) || (output == NULL))
		return -1;

	memcpy(output, argv[0], INTSIZ-1); // values go out of scope
	return 0;
}

// sql_row_count - counts the number of rows in a view
//
// NOTE: Create a view first. See sql_creat_view_on_decl() above.
//
bool sql_row_count(char *viewname, char *count)
{
	bool rval;
	char *zsql = sqlite3_mprintf("select count (id) from %q", viewname);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_process_row_count, (void *)count);
	sqlite3_free(zsql);
	return rval;
}

bool sql_open(const char *sqlfilename)
{
	int rval = sqlite3_open_v2
			(sqlfilename, &db, SQLITE_OPEN_READWRITE, NULL);

	if (rval != SQLITE_OK) {
		fprintf(stderr, "sqlite3_open_v2() returned %s\n",
			sqlite3_errstr(rval));
		return false;
	}
	return rval;
}

inline void sql_close(sqlite3 *db)
{
	sqlite3_close(db);
}

/*****************************************************
** Mine the database and Format the Results
******************************************************/

static char *indent(int padsiz)
{
	static char buf[DECLSIZ];

	memset(buf, 0, DECLSIZ);
	padsiz = min(padsiz, DECLSIZ);

	if (padsiz < 1)
		return buf;

	while(padsiz--)
		buf[padsiz] = ' ';

	return buf;
}

static int get_ancestry(struct row* prow)
{
	int i;
	int level;
	char **eptr = NULL;
	struct table *ptbl;

	level = strtoul(prow->level, eptr, 10);
	ptbl = new_table(level+1);

	// Put the row passed in as an argument into the first
	// row of the new table. Then go get its ancestors.
	copy_row(ptbl->rows, prow);

	for (i = 0; i < level; ++i) {
		char *parentid = ptbl->rows->parentid;
		++ptbl->rows;
		sql_get_rows_on_id((char *)sql_get_kabitable(),
				   parentid,
				   (void*)ptbl->rows);
	}

	for (i = level; i >= 0; --i) {
		int curlvl = strtoul(ptbl->rows->level, eptr, 10);

		if (curlvl < 4)
			printf("%s%s %s\n", indent(curlvl-1),
			       ptbl->rows->prefix, ptbl->rows->decl);
		else
			printf("%s%s\n", indent(curlvl-1), ptbl->rows->decl);
		--ptbl->rows;
	}
	return 0;
}

static int start(char *declstr, char *filename)
{
	int i;
	int rowcount;
	char countstr[INTSIZ];
	char *viewname = "searchview";
	struct row *prow = new_row();

	sql_open(filename);

	sql_create_view_on_decl(viewname, declstr);
	sql_row_count(viewname, countstr);
	sscanf(countstr, "%d", &rowcount);

	for (i = 0; i < rowcount; ++i) {
		memset(prow, 0, sizeof(struct row));
		sql_get_one_row(viewname, i, prow);
		get_ancestry(prow);

	}

	delete_row(prow);
	return 0;
}

/*****************************************************
** Command line option parsing
******************************************************/

static int parse_opt(char opt, int state)
{
	int optstatus = 1;

	switch (opt) {
	case '0' : kb_limit = 0;
		   break;
	case '1' : kb_limit = 10;
		   break;
	case '2' : kb_limit = 100;
		   break;
	case '3' : kb_limit = 1000;
		   break;
	case 'v' : kb_verbose = state;
		   break;
	case 'h' : puts(helptext);
		   exit(0);
	default  : optstatus = -1;
		   break;
	}

	return optstatus;
}

static int get_options(char **argv)
{
	int state = 0;		// on = 1, off = 0
	int index = 0;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		char *argstr = &(*argv++)[1];

		// Trailing '-' sets state to OFF (0) for switches.
		state = argstr[strlen(argstr)-1] != '-';

		for (i = 0; argstr[i]; ++i)
			parse_opt(argstr[i], state);
	}
	return index;
}

/*****************************************************
** main
******************************************************/

int main(int argc, char **argv)
{
	char *filename;
	char *declstr;
	int argindex = 0;

	DBG(setbuf(stdout, NULL);)

	if (argc <= 1) {
		puts(helptext);
		exit(0);
	}

	++argv;
	get_options(&argv[0]);

	argindex = get_options(&argv[0]);
	argv += argindex;
	argc -= argindex;

	declstr = argv[0];
	filename = argv[1];

	start(declstr, filename);
	return 0;
}


