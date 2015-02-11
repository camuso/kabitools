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
kabi-lookup [options] -[c|d|e]symbol \n\
\n\
    Searches a kabi database for symbols. The results of the search \n\
    are printed to stdout and indented hierarchically.\n\
\n\
Options:\n\
    -b datafile - Optional sqlite database file. The default is \n\
		\"../kabi-data.sql\" relative to the top of the \n\
		kernel tree. \n\
    -c symbol - Counts the instances of the symbol in the kabi tree. \n\
    -d symbol - Prints to stdout every instance of the symbol. It is\n\
		advisable to use \"-c symbol\"first to see how many \n\
		there are. \n\
    -e symbol - Specific to EXPORTED functions. Prints the function, \a\
                and its argument list. With the -v verbose switch, \n\
                it will print all the descendants of arguments that \n\
                are compound data types. \n\
    -s symbol - Seeks a data structure and prints its members to \n\
                stdout. With the -v switch, all descendants of all \n\
                members are also printed. \n\
    -w  whole words only, default is \"match any and all\" \n\
    -v  verbose lists all descendants of a symbol. \n\
    -h  this help message.\n\
\n";

enum kbflags {
	KB_COUNT	= 1 << 0,
	KB_DECL		= 1 << 1,
	KB_EXPORTS	= 1 << 2,
	KB_STRUCT	= 1 << 3,
	KB_VERBOSE	= 1 << 4,
	KB_WHOLE_WORD	= 1 << 5,
	KB_DATABASE	= 1 << 6,
};

static enum kbflags kb_flags = 0;
static const int kb_exemask  = KB_COUNT | KB_DECL | KB_EXPORTS | KB_STRUCT;
enum exemsg {
	EXE_OK,
	EXE_ARG2BIG,
	EXE_ARG2SML,
	EXE_CONFLICT,
	EXE_BADFORM,
	EXE_NOFILE,
	EXE_NOTFOUND,
	EXE_2MANY,
};

static int kb_argmask =	(1 << EXE_ARG2BIG)  |
			(1 << EXE_ARG2SML)  |
			(1 << EXE_CONFLICT) |
			(1 << EXE_BADFORM);

static const char *errstr[] = {
	[EXE_ARG2BIG]	= "Too many arguments",
	[EXE_ARG2SML]	= "Not enough arguments",
	[EXE_CONFLICT]	= "You entered conflicting switches",
	[EXE_BADFORM]	= "Badly formed argument list",
	[EXE_NOFILE]	= "Seeking \"%s\", but cannot open database file %s\n",
	[EXE_NOTFOUND]	= "\"%s\" cannot be found in database file %s\n",
	[EXE_2MANY]	= "Too many items match \"%s\" in database %s."
			  " Be more specific.\n",
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
	COL_ROWID,
	COL_LEVEL,
	COL_LEFT,
	COL_RIGHT,
	COL_FLAGS,
	COL_PREFIX,
	COL_TYPE = COL_PREFIX,
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
	char rowid[INTSIZ];
	char level[INTSIZ];
	char left[INTSIZ];
	char right[INTSIZ];
	char flags[INTSIZ];
	union {
		char prefix[PFXSIZ];
		char type[DECLSIZ];
	};
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

// copy row (dest, source)
void copy_row(struct row *drow, struct row *srow)
{
	strncpy(drow->rowid,	  srow->rowid,      INTSIZ-1);
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
static char *kabitypes = "kabitype";

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

	memcpy(output, argv[0], INTSIZ);

	return 0;
}

int sql_process_data(void *array, int argc, char **argv, char **colnames)
{
	int i;
	char **strary = (char **)array;

	if (!argc || !colnames)
		return -1;

	for (i = 0; i < argc; ++i) {
		DBG(printf("%s ", argv[i]);)
		strcpy(strary[i], argv[i]);
	}
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
		case COL_ROWID	    :
			strncpy(pr->level, argv[i], INTSIZ-1);
			break;
		case COL_LEVEL	    :
			strncpy(pr->level, argv[i], INTSIZ-1);
			break;
		case COL_LEFT	    :
			strncpy(pr->left, argv[i], INTSIZ-1);
			break;
		case COL_RIGHT	    :
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
//       This function will print rows in the order in which they are
//       returned by the database query that called it, and only the
//       FILE, EXPORTED, and ARG prefixes will be printed.
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
	ZS_INNER,
	ZS_OUTER,
	ZS_INNER_LEVEL,
	ZS_OUTER_LEVEL,
	ZS_VIEWDECL_ANY,
	ZS_VIEWDECL_WORD,
	ZS_VIEWDECL_ANY_LEVEL,
	ZS_VIEWDECL_WORD_LEVEL,
	ZS_VIEW_INNER,
	ZS_VIEW_OUTER,
	ZS_VIEW_INNER_LEVEL,
	ZS_VIEW_OUTER_LEVEL,
};

static char *zstmt[] = {
[ZS_DECL_ANY]  =
	"select * from %q where decl like '%%%q%%'",
[ZS_DECL_WORD] =
	"select * from %q where decl like '%%%q %%'",
[ZS_INNER] =
	"select * from %q where left >= %lu AND right <= %lu",
[ZS_OUTER] =
	"select * from %q where left <= %lu AND right >= %lu",
[ZS_INNER_LEVEL] =
	"select * from %q where left >= %lu AND right <= %lu AND level %s",
[ZS_OUTER_LEVEL] =
	"select * from %q where left <= %lu AND right >= %lu AND level %s",
[ZS_VIEWDECL_ANY] =
	"create temp view %q as select * from %q where decl like '%%%q%%'",
[ZS_VIEWDECL_WORD] =
	"create temp view %q as select * from %q where decl like '%%%q %%'",
[ZS_VIEWDECL_ANY_LEVEL]  =
	"create temp view %q as select * from %q where decl like '%%%q%%'"
	" AND level %s",
[ZS_VIEWDECL_WORD_LEVEL] =
	"create temp view %q as select * from %q where decl like '%%%q %%'"
	" AND level %s",
[ZS_VIEW_INNER] =
	"create temp view %q as select * from %q where"
	" left >= %lu AND right <= %lu",
[ZS_VIEW_OUTER] =
	"create temp view %q as select * from %q"
	" where left >= %lu AND right <= %lu",
[ZS_VIEW_INNER_LEVEL] =
	"create temp view %q as select * from %q where"
	" left >= %lu AND right <= %lu AND level %s",
[ZS_VIEW_OUTER_LEVEL] =
	"create view %q as select * from %q where"
	" left >= %lu AND right <= %lu AND level %s",
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
				zstmt[ZS_DECL_WORD] :
				zstmt[ZS_DECL_ANY];
	char *zsql = sqlite3_mprintf(stmt, viewname, declstr);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_process_row, output);
	sqlite3_free(zsql);
	return rval;
}

// sql_get_nest - create a view based on the nest boundaries
//                           can be inner or outer, depending on zs
//
// table - name of table or view from which to extract the nest
// left  - leftmost boundary of nest
// right - rightmost boundary of nest
// zs    - an enum that selects a command string from the zstmt array
// level - used if the selected zs needs it. Otherwise NULL to satisfy
//         the argument list.
// dest  - data area to pass to the callback, which copy the data from
//         the view into the data area.
//
bool sql_get_nest(const char *view, long left, long right,
		  enum zstr zs, char *level, void *dest)
{
	bool rval;
	char *zsql;

	if (level)
		zsql = sqlite3_mprintf(zstmt[zs], view, left, right, level);
	else
		zsql = sqlite3_mprintf(zstmt[zs], view, left, right);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_print_row, dest);
	sqlite3_free(zsql);
	return rval;
}

// sql_create_view_on_nest - create a view based on the nest boundaries
//                           can be inner or outer, depending on zs
//
// view	 - name of view to create
// table - name of table from which to create the view
// left  - leftmost boundary of nest
// right - rightmost boundary of nest
// zs    - an enum that selects a command string from the zstmt array
// level - used if the selected zs needs it. Otherwise NULL to satisfy
//         the argument list.
//
bool sql_create_view_on_nest(char *view, char *table,
				   long left, long right,
				   enum zstr zs, char *level)
{
	bool rval;
	char *zsql;

	if (level)
		zsql = sqlite3_mprintf(zstmt[zs], view, table,
				       left, right, level);
	else
		zsql = sqlite3_mprintf(zstmt[zs], view, table, left, right);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, 0, 0);
	sqlite3_free(zsql);
	return rval;
}

// sql_create_view_on_decl - create a view based on the decl column of a table
//
// view	 - name of view to create
// table - name of table from which to create the view
// decl  - string in decl column to be sought.
// level - used if the selected zs needs it. Otherwise NULL to satisfy
//         the argument list.
//
bool sql_create_view_on_decl(char *view, char *table,
			     char *declstr, char *level)
{
	bool rval;
	char *zsql;
	char *stmt;

	if  (level) {
		stmt = kb_flags & KB_WHOLE_WORD ?
				zstmt[ZS_VIEWDECL_WORD_LEVEL] :
				zstmt[ZS_VIEWDECL_ANY_LEVEL];
		zsql = sqlite3_mprintf(stmt, view, table, declstr, level);
	} else {
		stmt = kb_flags & KB_WHOLE_WORD ?
				zstmt[ZS_VIEWDECL_WORD] :
				zstmt[ZS_VIEWDECL_ANY];
		zsql = sqlite3_mprintf(stmt, view, table, declstr);
	}

	DBG(puts(zsql);)
	rval = sql_exec(zsql, 0, 0);
	sqlite3_free(zsql);
	return rval;
}

bool sql_exists(char *type, char *name, bool temp)
{
	char answer[INTSIZ];
	char *master = temp ? "sqlite_temp_master" : "sqlite_master";
	char *zsql = sqlite3_mprintf("select count() from %s where "
				     "type='%q' AND name='%q'",
				     master, type, name);

	sql_exec(zsql, sql_process_field, (void *)answer);
	sqlite3_free(zsql);

	if (!strcmp(answer, "0"))
		return false;

	return true;
}

// sql_row_count - counts the number of rows in a view
//
// NOTE: Create a view first. See sql_creat_view_on_decl() above.
//
bool sql_row_count(char *view, char *count)
{
	bool rval;
	char *zsql = sqlite3_mprintf("select count () from %q", view);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, sql_process_field, (void *)count);
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

static bool process_row(struct row *prow, enum zstr zs, char *level)
{
	long left;
	long right;
	struct row *pr = new_row();

	memset(pr, 0, sizeof(struct row));
	copy_row(pr, prow);
	sscanf(pr->left,  "%lu", &left);
	sscanf(pr->right, "%lu", &right);

	sql_get_nest(kabitable, left, right, zs, level, (void *)prow);
	delete_row(pr);
	return true;
}

static bool get_struct(struct row *prow)
{
	int i;
	int count;
	char *view = "typ";
	char *t1;

	t1 = strchr(prow->decl, ' ');
	++t1;
	t1 = strchr(t1, ' ');
	*t1 = '\0';

	if (sql_exists("view", "typ", true))
		sql_exec("drop view typ", 0, 0);

	sql_create_view_on_decl(view, kabitypes, prow->decl, ">= 2");
	get_count(view, &count);

	for (i = 0; i < count; ++i) {
		memset(prow, 0, sizeof(struct row));
		sql_get_one_row(view, i, prow);
		process_row(prow, ZS_INNER, NULL);
	}

	sql_exec("drop view typ", 0, 0);
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

	if (sql_exists("view", "kt", true))
		sql_exec("drop view kt", 0, 0);

	prow = new_row();
	sql_create_view_on_decl(view, kabitable, declstr, NULL);
	get_count(view, &rowcount);

	for (i = 0; i < rowcount; ++i) {
		memset(prow, 0, sizeof(struct row));
		sql_get_one_row(view, i, prow);
		process_row(prow, ZS_OUTER, NULL);
	}

	delete_row(prow);
	sql_exec("drop view kt", 0, 0);
	sql_close(db);
	return EXE_OK;
}

static int exe_exports(char *declstr, char *datafile)
{
	int rval = EXE_OK;
	int count;
	char *view = "kt";
	struct row *prow;

	if (!sql_open(datafile))
		return EXE_NOFILE;

	if (sql_exists("view", "kt", true))
		sql_exec("drop view kt", 0, 0);

	sql_create_view_on_decl(view, kabitable, declstr, "== 1");
	get_count(view, &count);

	if (count == 0) {
		rval = EXE_NOTFOUND;
		goto out;
	}

	if (count > 1) {
		rval = EXE_2MANY;
		goto out;
	}

	prow = new_row();
	sql_get_one_row(view, 0, prow);
	printf("FILE: %s\n", prow->parentdecl);

	if (kb_flags & KB_VERBOSE) {
		int i;
		long left;
		long right;

		sscanf(prow->left,  "%lu", &left);
		sscanf(prow->right, "%lu", &right);
		sql_exec("drop view kt", 0 , 0);
		sql_create_view_on_nest(view, kabitable,
					left, right,
					ZS_VIEW_INNER_LEVEL, "<= 2");
		get_count(view, &count);
		sql_get_one_row(view, 0, prow);
		process_row(prow, ZS_OUTER_LEVEL, "== 1");

		for (i = 0; i < count; ++i) {
			memset(prow, 0, sizeof(struct row));
			sql_get_one_row(view, i, prow);
			if (!(strstr(prow->decl, "struct ")) &&
			    !(strstr(prow->decl, "union ")))
				continue;
			get_struct(prow);
		}
	}
	else
		process_row(prow, ZS_INNER_LEVEL, "<= 2");

	delete_row(prow);
out:
	sql_exec("drop view kt", 0, 0);
	sql_close(db);
	return rval;
}

int exe_count(char *declstr, char *datafile)
{
	int count;
	char *view = "kt";
	struct row *prow;

	if (!sql_open(datafile))
		return EXE_NOFILE;

	if (sql_exists("view", "kt", true))
		sql_exec("drop view kt", 0, 0);

	prow = new_row();
	sql_create_view_on_decl(view, kabitable, declstr, NULL);
	get_count(view, &count);
	printf("There are %d symbols matching \"%s\".\n", count, declstr);
	delete_row(prow);
	sql_exec("drop view kt", 0, 0);
	sql_close(db);
	return EXE_OK;
}

static int exe_struct(char *declstr, char *datafile)
{
	int level;
	char *view = "kt";
	char lvlstr[INTSIZ];
	struct row *prow;

	if (!sql_open(datafile))
		return EXE_NOFILE;

	if (sql_exists("view", "kt", true))
		sql_exec("drop view kt", 0, 0);

	sql_create_view_on_decl(view, kabitypes, declstr, "> 2");
	prow = new_row();
	sql_get_one_row(view, 0, prow);
	sscanf(prow->level, "%d", &level);
	sprintf(lvlstr, "%d", level+1);

	if (kb_flags & KB_VERBOSE)
		process_row(prow, ZS_INNER, NULL);
	else
		process_row(prow, ZS_INNER_LEVEL, lvlstr);

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

static bool parse_opt(char opt, char ***argv)
{
	switch (opt) {
	case 'b' : datafile = *((*argv)++);
		   break;
	case 'c' : kb_flags |= KB_COUNT;
		   declstr = *((*argv)++);
		   break;
	case 'd' : kb_flags |= KB_DECL;
		   declstr = *((*argv)++);
		   break;
	case 'e' : kb_flags |= KB_EXPORTS;
		   declstr = *((*argv)++);
		   break;
	case 's' : kb_flags |= KB_STRUCT;
		   declstr = *((*argv)++);
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
	char *argstr;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		argstr = &(*argv++)[1];

		for (i = 0; argstr[i]; ++i)
			if (!parse_opt(argstr[i], &argv))
				return false;
		if (!*argv)
			break;
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
	if ((1 << err) & kb_argmask) {
		printf("\n%s. You typed ...\n  ", errstr[err]);
		print_cmdline();
		printf("\nPlease read the help text below.\n%s", helptext);
	} else {
		printf (errstr[err], declstr, datafile);
	}
}

static int process_args(int argc, char **argv)
{
	int argindex = 0;

	orig_argc = argc;
	orig_argv = argv;

	// skip over argv[0], which is the invocation of this app.
	++argv; --argc;

	if (argc < 2)
		return EXE_ARG2SML;

	if (!get_options(&argv[0], &argindex))
		return EXE_BADFORM;

	return 0;
}

static int execute()
{
	switch (kb_flags & kb_exemask) {
	case KB_COUNT   : return exe_count(declstr, datafile);
	case KB_DECL    : return exe_decl(declstr, datafile);
	case KB_EXPORTS : return exe_exports(declstr, datafile);
	case KB_STRUCT  : return exe_struct(declstr, datafile);
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
		return 1;
	}

	if ((err = execute())) {
		print_cmd_errmsg(err);
		return 1;
	}

	return 0;
}
