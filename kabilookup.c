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

// For this compilation unit only
//
#ifdef true
#undef true
#endif
#ifdef false
#undef false
#endif
#define true 1
#define false 0

#if !defined(NDEBUG)
#define DBG(x) x
#define RUN(x)
#else
#define DBG(x)
#define RUN(x) x
#endif

typedef unsigned int bool;

/*****************************************************
** Global declarations
******************************************************/

static const char *helptext ="\
\n\
kabilookup [options] searchstr datafile\n\
\n\
Searches a kabitree database to lookup declaration strings containing \n\
the user's input. The search is continued until all containers of the\n\
declaration are found. The results of the search are printed to stout\n\
indented hierarchically.\n\
\n\
    searchstr - String to lookup. \n\
    datafile  - Database file containing a kabitree table defined as follows\n\
\n\
                id         - Unique number to identify the record. \n\
                parentid   - Unique number identifying the record having the\n\
                             \"parent\" or container of the object declared \n\
		           in the current record.\n\
                level      - Nested level of the object indicates how deep\n\
                             the object is in the primordial ancestor's tree.\n\
                flags      - Used by code to identify record types (e.g. \n\
                             \"FILE\", \"EXPORTED\", etc). Also used for code\n\
                             flow. \n\
                prefix     - String containing the record type. \n\
                decl       - String containing the declaration of the object\n\
                             in the kernel. \n\
                parentdecl - String containing the declaration of the \n\
                             containing object, where there is one. Files\n\
                             do not have containers, for the purposes of this\n\
                             application.\n\
Options:\n\
    -w        whole words only \n\
    -i        ignore case \n\
    -v        verbose (default): lists all descendants as well as ancestors \n\
    -h        this help message.\n\
\n";

static bool kp_verbose = true;


/*****************************************************
** global sqlite3 data
******************************************************/
static struct sqlite3 *db = NULL;
static sqlite3_stmt *stmt_decl_search;
static sqlite3_stmt *stmt_id_search;
static sqlite3_stmt *stmt_get_row;
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

#define PFXSIZ 16
#define DECLSIZ 256

struct row {
	long id;
	long parentid;
	int level;
	int flags;
	char prefix[PFXSIZ];
	char decl[DECLSIZ];
};

/*****************************************************
** SQLite utilities and wrappers
******************************************************/
// if this were c++, db and table would be private members
// of a sql class, accessible only by member function calls.

inline struct sqlite3 *sql_get_db()
{
	return db;
}

inline void sql_set_table(const char *tablename)
{
	table = (char *)tablename;
}

inline const char *sql_get_table()
{
	return (const char *)table;
}

bool sql_exec(const char *stmt)
{
	char *errmsg;
	int ret = sqlite3_exec(db, stmt, 0, 0, &errmsg);

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
	strcpy(output, argv[0]);
	return 0;
}

int sql_process_row(void *output, int argc, char **argv, char **colnames)
{
	int i;
	int rowarysize = argc * sizeof(struct row);
	void *hunk = malloc(rowarysize);
	struct row **rows = (struct row **)hunk;

	printf("argc: %d\n", argc);
	memset(rows, 0, rowarysize);

	for (i = 0; i < argc; ++i)
	{
		printf("%s ", colnames[i]);
	}

	putchar('\n');

	for (i = 0; i < argc; ++i)
	{
		printf("%s ", argv[i]);
	}
	putchar('\n');
	putchar('\n');

	//strcpy(output, argv[0]);
	output = hunk;
	return 0;
}

bool sql_get_row(char *searchstr, char *output)
{
	char *errmsg;
	char *zsql = sqlite3_mprintf
			("select id,parentid,level,prefix,decl,parentdecl "
			 "from kabitree where decl like \'%%%q%%\'",
			  searchstr);

	printf("zsql: %s\n", zsql);
	getchar();
	int retval = sqlite3_exec
			(db, zsql, sql_process_row, (void *)output, &errmsg);

	if (retval != SQLITE_OK) {
		fprintf(stderr,
			"\nError in statement: %s [%s].\n", zsql, errmsg);
		sqlite3_free(errmsg);
		return false;
	}
	return true;
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
** Command line option parsing
******************************************************/

static int parse_opt(char opt, int state)
{
	int optstatus = 1;

	switch (opt) {
	case 'v' : kp_verbose = state;	// kp_verbose is global to this file
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


static int get_ancestry(int level, long parentid)
{
	return 0;
}

static int start(char *searchstr, char *filename)
{
	struct row **rows = NULL;

	sql_open(filename);
	sql_get_row(searchstr, (void *)rows);

	return 0;
}

/*****************************************************
** main
******************************************************/

int main(int argc, char **argv)
{
	char *filename;
	char *searchstr;
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

	searchstr = argv[0];
	filename = argv[1];

	start(searchstr, filename);
	return 0;
}


