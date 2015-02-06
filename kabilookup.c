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

static bool kb_verbose = true;
static bool kb_whole_word = false;
static int kb_limit = 0;
static bool kb_exports_only = false;

static char *indent(int padsiz);

/*****************************************************
** global sqlite3 data
******************************************************/
static struct sqlite3 *db = NULL;
static const char *table = "kabitree";

// schema enumeration
enum schema {
	COL_LEVEL,
	COL_ID,
	COL_PARENTID,
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
	char level[INTSIZ];	//	:
	char id[INTSIZ];	// Returned by sqlite as hex char strings
	char parentid[INTSIZ];	//	:
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
	free(tptr->array);
	free(tptr);
}

static void copy_row(struct row *drow, struct row *srow)
{
	strncpy(drow->level,      srow->level,      INTSIZ-1);
	strncpy(drow->id,         srow->id,         INTSIZ-1);
	strncpy(drow->parentid,   srow->parentid,   INTSIZ-1);
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
		case COL_ID	    :
			strncpy(pr->id, argv[i], INTSIZ-1);
			break;
		case COL_PARENTID   :
			strncpy(pr->parentid, argv[i], INTSIZ-1);
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
	if (!argc || output || !colnames)
		return -1;

	if (strtoul(argv[COL_LEVEL],0,0) == 0)
		printf("\n%s ", argv[COL_PREFIX]);

	printf("%s%s\n",
	       indent((int)strtoul(argv[COL_LEVEL],0,0)),
	       argv[COL_DECL]);
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

		if (curlvl < 3)
			printf("%s%s %s\n", indent(curlvl-1),
			       ptbl->rows->prefix, ptbl->rows->decl);
		else if (kb_verbose)
			printf("%s%s\n", indent(curlvl-1), ptbl->rows->decl);
		--ptbl->rows;
	}

	//delete_table(ptbl);
	return 0;
}

static int start(char *declstr, char *datafile)
{
	int i;
	int rowcount;
	char countstr[INTSIZ];
	char *viewname = "searchview";
	struct row *prow = new_row();

	sql_open(datafile);

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

static int do_exports_only(const char *declstr,
			   const char *exportspath,
			   const char *datafile)
{
	bool rval;
	char *zsql ;

	if (! sql_open(datafile))
		return -1;

	zsql = sqlite3_mprintf("CREATE temp VIEW A as select * from %q "
			       "where level=0 and decl like '%%%q%%'",
			       kabitable, exportspath);
	DBG(puts(zsql);)
	if (!(rval = sql_exec(zsql, 0, 0)))
		goto out;

	zsql = sqlite3_mprintf("CREATE temp VIEW B as select * from %q "
			       "where level=1 and decl like '%%%q%%'",
			       kabitable, declstr);
	DBG(puts(zsql);)
	if (!(rval = sql_exec(zsql, 0, 0)))
		goto out;

	zsql = sqlite3_mprintf("CREATE temp VIEW C as select * from %q "
			       "where level=2 and decl like '%%%q%%'",
			       kabitable, declstr);
	DBG(puts(zsql);)
	if (!(rval = sql_exec(zsql, 0, 0)))
		goto out;

	zsql = sqlite3_mprintf("select * from A union "
			       "select * from B union "
			       "select * from C order by id");
	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_print_row, 0);
out:
	sqlite3_free(zsql);
	return rval ? 0 : -1;
}

/*****************************************************
** Command line option parsing
******************************************************/

// Mimic c++ encapsulation. These variables are invisible to subroutines
// above here in the file, unless they are passed by call.
static char *declstr;		// string or substring we're seeking
static char *datafile;		// name of the database file
static char *exportspath;	// optional exports only view
static int orig_argc;
static char **orig_argv;
enum cmderr {CMDERR_TOOMANY, CMDERR_TOOFEW};
static const char *errstr[] = {
	"You entered too many arguments",
	"You entered too few arguments"
	"\0"
};

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
	case 'x' : kb_exports_only = state;
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

static void print_cmdline() {
	int i = 0;
	for (i = 0; i < orig_argc; ++i)
		printf(" %s", orig_argv[i]);
}

static void print_cmd_errmsg(enum cmderr err)
{
	printf("\n%s. You typed ...\n  ", errstr[err]);
	print_cmdline();
	printf("\nPlease read the help text below.\n%s", helptext);
}

static bool process_args(int argc, char **argv)
{
	int argindex = 0;

	orig_argc = argc;
	orig_argv = argv;

	if (argc <= 2) {
		puts(helptext);
		exit(0);
	}

	++argv; --argc;
	get_options(&argv[0]);

	argindex = get_options(&argv[0]);
	argv += argindex;
	argc -= argindex;

	switch (argc) {
	case 2: if (kb_exports_only) {
			print_cmd_errmsg(CMDERR_TOOFEW);
			return false;
		}
		declstr = argv[0];
		datafile = argv[1];
		break;
	case 3: if (!kb_exports_only) {
			print_cmd_errmsg(CMDERR_TOOMANY);
			return false;
		}
		declstr = argv[0];
		exportspath = argv[1];
		datafile = argv[2];
		break;
	default: return false;
		 break;
	}

	return true;
}

/*****************************************************
** main
******************************************************/

int main(int argc, char **argv)
{
	DBG(setbuf(stdout, NULL);)

	if (!process_args(argc, argv))
		return -1;

	if (kb_exports_only)
		do_exports_only(declstr, exportspath, datafile);
	else
		start(declstr, datafile);
	sql_close(db);
	return 0;
}
