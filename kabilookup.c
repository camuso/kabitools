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
#define true  1
#define false 0

typedef unsigned int bool;

/*****************************************************
** Global declarations
******************************************************/

static const char *helptext ="\
\n\
kabi-lookup [options] declstr datafile \n\
  e.g. \n\
  $ kabi-lookup \"struct acpi\" ../kabi-data.sql \n\
\n\
kabi-lookup -x [options] declstr exportspath datafile \n\
  e.g. \n\
  $ kabi-lookup -x \"struct device\" \"drivers/pci\" ../kabi-data.sql\n\
\n\
    Searches a kabitree database to lookup declaration strings \n\
    containing the user's input. In verbose mode (default), the search \n\
    is continued until all ancestors (containers) of the declaration are \n\
    found. The results of the search are printed to stdout and indented \n\
    hierarchically.\n\
\n\
    declstr     - Required. Declaration to lookup. Use double quotes \"\" \n\
                  around compound strings. \n\
    exportspath - Only used with -x option (see below). When using the \n\
                  -x option, this string must be the 2nd argument. \n\
    datafile    - Required. Database file containing the tree of exported \n\
                  functions and any and all symbols explicitly or implicitly \n\
                  affected. This file should be created by the kabi-data.sh \n\
                  tool. \n\
Options:\n\
        Limits on the number of records displayed: \n\
    -0  no limits \n\
    -1  10 \n\
    -2  100 \n\
    -3  1000 \n\
\n\
        Other options \n\
    -x  Requires exports path string as 2nd argument. Lists only exported \n\
        functions and their arguments for the given path, e.g.\n\
          $ kabi-lookup \"struct device\" \"drivers/pci\" ../kabi-data.sql\n\
    -w  whole words only, default is \"match any and all\" \n\
    -i  ignore case \n\
    -v  verbose (default): lists all ancestors \n\
    -v- to disable verbose \n\
    -h  this help message.\n\
\n";

enum kbflags {
	KB_COUNT,
	KB_DECL,
	KB_EXPORTS,
	KB_VERBOSE,
	KB_WHOLE_WORD,
	KB_DATABASE,
};

static enum kbflags kb_flags = 0;
static const int kb_exemask  = (1 << KB_COUNT) |
			       (1 << KB_DECL)  |
			       (1 << KB_EXPORTS);
enum exemsg {
	EXE_OK,
	EXE_NOFILE,
	EXE_NOTFOUND,
	EXE_2MANY,
	EXE_ARG2BIG,
	EXE_ARG2SML,
	EXE_CONFLICT,
};

static int kb_argmask =	(1 << EXE_ARG2BIG) |
			(1 << EXE_ARG2SML) |
			(1 << EXE_CONFLICT);

static const char *errstr[] = {
	[EXE_NOFILE]	= "Cannot open database file %s\n",
	[EXE_NOTFOUND]	= "%s cannot be found in database file %s\n",
	[EXE_2MANY]	= "Too many items match %s. Be more specific.\n",
	[EXE_ARG2BIG]	= "Too many rguments",
	[EXE_ARG2SML]	= "Not enough arguments",
	[EXE_CONFLICT]	= "You entered conflicting switches",
	"\0"
};

enum exemsg;
static char *indent(int padsiz);
static void print_cmd_errmsg(enum exemsg err);
static void print_cmdline();

/*****************************************************
** global sqlite3 data
******************************************************/
static struct sqlite3 *db = NULL;
static const char *table = "kabitree";

// schema enumeration
enum schema {
	COL_LEVEL,
	COL_LEFT,
	COL_RIGHT,
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
	char level[INTSIZ];
	char left[INTSIZ];
	char right[INTSIZ];
	char flags[INTSIZ];
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
	free(tptr->array);
	free(tptr);
}

static void copy_row(struct row *drow, struct row *srow)
{
	strncpy(drow->level,	  srow->level,      INTSIZ-1);
	strncpy(drow->left,       srow->left,       INTSIZ-1);
	strncpy(drow->right,      srow->right,	    INTSIZ-1);
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

static char *kabitable = "kabitree";

inline struct sqlite3 *sql_get_db()
{
	return db;
}

inline void sql_set_kabitable(const char *tablename)
{
	kabitable = (char *)tablename;
}

inline char *sql_get_kabitable()
{
	return kabitable;
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
	if (argc != 1 || !colnames)
		return -1;

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

	if (!argc || !colnames)
		return -1;

	for (i = 0; i < argc; ++i) {
		DBG(printf("%s ", argv[i]);)

		switch (i){
		case COL_LEVEL	    :
			strncpy(pr->level, argv[i], INTSIZ-1);
			break;
		case COL_LEFT	    :
			strncpy(pr->left, argv[i], INTSIZ-1);
			break;
		case COL_RIGHT   :
			strncpy(pr->right, argv[i], INTSIZ-1);
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

// sql_print_row - callback to print one row from a select call
//
// NOTE: the "output" parameter is unused here.
//       also, this function will print rows in the order in which they
//       are returned by the database query that called it, and only
//	 the FILE prefix will be printed and none of the other prefixes.
//
int sql_print_row(void *output, int argc, char **argv, char **colnames)
{
	int pad = (int)strtoul(argv[COL_LEVEL],0,0);
	char *padstr = indent(pad);

	if (!argc || !output || !colnames)
		return -1;

	if (strtoul(argv[COL_LEVEL],0,0) <= 2)
		printf("%s%s %s\n", padstr, argv[COL_PREFIX], argv[COL_DECL]);
	else
		printf("%s%s\n", padstr, argv[COL_DECL]);
	return 0;
}

enum zstr{
	ZS_DECL_ANY,
	ZS_DECL_WORD,
	ZS_DECL_ANY_NESTED,
	ZS_DECL_WORD_NESTED,
	ZS_OUTER,
	ZS_OUTER_AT,
	ZS_OUTER_ATBELOW,
	ZS_INNER,
	ZS_INNER_AT,
	ZS_INNER_ATBELOW,
};

static char *zstmt[] = {
	"select * from %q where decl like \'%%%q%%\'",
	"select * from %q where decl like \'%%%q %%\'",
	"select * from %q where left <= %lu AND right >= %lu",
	"select * from %q where left <= %lu AND right >= %lu AND level == %d",
	"select * from %q where left <= %lu AND right >= %lu AND level <= %d",
	"select * from %q where left >= %lu AND right <= %lu",
	"select * from %q where left >= %lu AND right <= %lu AND level == %d",
	"select * from %q where left >= %lu AND right <= %lu AND level <= %d",
	"create view %q as select * from %q where decl like \'%%%q%%\' "
		"and level > 2"
	"create view %q as select * from %q where decl like \'%%%q %%\' "
		"and level > 2"
};

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

bool sql_get_rows_on_decl(char *viewname, char *declstr, void *output)
{
	bool rval;
	char *stmt = kb_flags & KB_WHOLE_WORD ?
				zstmt[ZS_DECL_WORD_NESTED] :
				zstmt[ZS_DECL_ANY_NESTED];
	char *zsql = sqlite3_mprintf(stmt, viewname, declstr);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_process_row, output);
	sqlite3_free(zsql);
	return rval;
}


bool sql_get_nest_level(const char *view, long left, long right, int level,
			enum zstr zs, void *dest)
{
	bool rval;
	char *zsql = sqlite3_mprintf(zstmt[zs], view, left, right, level);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_print_row, dest);
	sqlite3_free(zsql);
	return rval;
}

bool sql_get_nest(const char *view, long left, long right,
		  enum zstr zs, void *dest)
{
	bool rval;
	char *zsql = sqlite3_mprintf(zstmt[zs], view, left, right);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_process_row, dest);
	sqlite3_free(zsql);
	return rval;
}

bool sql_create_view_on_decl(char *viewname, char *declstr)
{
	bool rval;
	char *zsql;
	char *stmt = kb_flags & KB_WHOLE_WORD ?
				zstmt[ZS_DECL_WORD_NESTED] :
				zstmt[ZS_DECL_ANY_NESTED];

	zsql = sqlite3_mprintf(stmt, viewname, sql_get_kabitable(), declstr);
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
	if ((argc != 1) || !output || !colnames)
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
	char *zsql = sqlite3_mprintf("select count (left) from %q", viewname);

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
	return true;
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

static bool get_count(char *view, int *count)
{
	char countstr[INTSIZ];

	if (!sql_row_count(view, &countstr[0]))
		return false;
	sscanf(countstr, "%d", count);
	return true;
}

static bool process_row(struct row *prow, enum zstr zs)
{
	int level;
	long left;
	long right;

	sscanf(prow->level, "%d",  &level);
	sscanf(prow->left,  "%lu", &left);
	sscanf(prow->right, "%lu", &right);

	memset(prow, 0, sizeof(struct row));

	sql_get_nest_level(sql_get_kabitable(),
			   left, right, level, zs, prow);
	return true;
}

static int exe_decl(char *declstr, char *datafile)
{
	int i;
	int rowcount;
	char *view = "kt";
	struct row *prow;

	if (!sql_open(datafile))
		return EXE_NOFILE;

	prow = new_row();
	sql_create_view_on_decl(view, declstr);
	get_count(view, &rowcount);

	for (i = 0; i < rowcount; ++i) {
		memset(prow, 0, sizeof(struct row));
		sql_get_one_row(view, i, prow);
		process_row(prow, ZS_OUTER_ATBELOW);
	}

	delete_row(prow);
	sql_exec("drop view kt", 0, 0);
	sql_close(db);
	return EXE_OK;
}

static int exe_exports(char *declstr, char *datafile)
{
	int count;
	char *view = "kt";
	struct row *prow;

	if (!sql_open(datafile))
		return EXE_NOFILE;

	sql_create_view_on_decl(view, declstr);
	get_count(view, &count);

	if (count == 0)
		return EXE_NOTFOUND;

	if (count > 1)
		return EXE_2MANY;

	prow = new_row();
	sql_get_rows_on_decl(sql_get_kabitable(), declstr, (void *)prow);

	return EXE_OK;
}

int exe_count(char *declstr, char *datafile)
{
	int count;
	char *view = "kt";
	struct row *prow;

	if (!sql_open(datafile))
		return EXE_NOFILE;

	prow = new_row();
	sql_create_view_on_decl(view, declstr);
	get_count(view, &count);
	printf("There are %d symbols matching %s.\n", count, declstr);

	delete_row(prow);
	sql_exec("drop view kt", 0, 0);
	sql_close(db);
	return EXE_OK;
}

/*****************************************************
** Command line option parsing
******************************************************/

// Mimic c++ encapsulation. These variables are invisible to subroutines
// above here in the file, unless they are passed by call.
static char *datafile = "../kabi-data.sql";
static char *declstr;
static int orig_argc;
static char **orig_argv;

static int count_bits(unsigned mask)
{
	int count = 0;

	do {
		count += mask & 1;
	} while (mask >>= 1);

	return count;
}

// Check for mutually exclusive flags.
static bool check_flags()
{
	unsigned flg = kb_flags & kb_exemask;

	if (count_bits(flg) > 1)
		return false;

	return true;
}

static bool parse_opt(char opt, char **argv)
{
	switch (opt) {
	case 'b' : datafile = *(++argv);
		   break;
	case 'c' : kb_flags |= KB_COUNT;
		   break;
	case 'd' : kb_flags |= KB_DECL;
		   declstr = *(++argv);
		   break;
	case 'e' : kb_flags |= KB_EXPORTS;
		   declstr = *(++argv);
		   break;
	case 'v' : kb_flags |= KB_VERBOSE;
		   break;
	case 'w' : kb_flags |= KB_WHOLE_WORD;
		   break;
	case 'h' : puts(helptext);
		   exit(0);
	default  : return false;
	}

	return check_flags();
}

static bool get_options(char **argv, int *idx)
{
	int index = 0;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		char *argstr = &(*argv++)[1];

		for (i = 0; argstr[i]; ++i)
			if (!parse_opt(argstr[i], argv))
				return false;
	}

	*idx = index;
	return true;
}

static void print_cmdline()
{
	int i = 0;
	for (i = 0; i < orig_argc; ++i)
		printf(" %s", orig_argv[i]);
}

static void print_cmd_errmsg(enum exemsg err)
{
	printf("\n%s. You typed ...\n  ", errstr[err]);
	print_cmdline();
	printf("\nPlease read the help text below.\n%s", helptext);
}

static int process_args(int argc, char **argv)
{
	int argindex = 0;

	orig_argc = argc;
	orig_argv = argv;

	// skip over argv[0], which is the invocation of this app.
	++argv; --argc;

	if (!get_options(&argv[0], &argindex))
		return EINVAL;

	return 0;
}

static int execute()
{
	switch (kb_flags & kb_exemask) {
	case KB_COUNT   : return exe_count(datafile, declstr);
	case KB_DECL    : return exe_decl(declstr, datafile);
	case KB_EXPORTS : return exe_exports(declstr, datafile);
	}
	return 0;
}

/*****************************************************
** main
******************************************************/

int main(int argc, char **argv)
{
	int err;
	DBG(setbuf(stdout, NULL);)

	if((err = process_args(argc, argv))) {
		print_cmd_errmsg(err);
		return -1;
	}

	return execute();
}
