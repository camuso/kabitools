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
#include <ctype.h>
#include <time.h>
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
kabi-lookup -[vw] -c|d|e|s symbol [-b datafile]\n\
    Searches a kabi database for symbols. The results of the search \n\
    are printed to stdout and indented hierarchically.\n\
\n\
    -c symbol   - Counts the instances of the symbol in the kabi tree. \n\
    -s symbol   - Prints to stdout every exported function that is implicitly \n\
                  or explicitly affected by the symbol. In verbose mode, the \n\
                  chain from the exported function to the symbol is printed.\n\
                  It is advisable to use \"-c symbol\" first. \n\
    -e symbol   - Specific to EXPORTED functions. Prints the function, \n\
                  and its argument list. With the -v verbose switch, it \n\
                  will print all the descendants of nonscalar arguments. \n\
    -d symbol   - Seeks a data structure and prints its members to stdout. \n\
                  The -v switch prints descendants of nonscalar members. \n\
    -v          - verbose lists all descendants of a symbol. \n\
    -w          - whole words only, default is \"match any and all\" \n\
    -b datafile - Optional sqlite database file. The default is \n\
                  \"../kabi-data.sql\" relative to top of kernel tree. \n\
    --no-dups   - A structure can appear more than once in the nest of an \n\
                  exported function, for example a pointer back to itself as\n\
                  parent to one of its descendants. This switch limits the \n\
                  appearance of an exported function to just once. \n\
    -h  this help message.\n";



/*****************************************************
** Enums and Flags
******************************************************/

// User input control flags
enum kbflags {
	KB_COUNT	= 1 << 0,
	KB_DECL		= 1 << 1,
	KB_EXPORTS	= 1 << 2,
	KB_STRUCT	= 1 << 3,
	KB_VERBOSE	= 1 << 4,
	KB_WHOLE_WORD	= 1 << 5,
	KB_DATABASE	= 1 << 6,
	KB_NODUPS	= 1 << 7,
	KB_ARGS		= 1 << 8,
};

static enum kbflags kb_flags = 0;
static const int kb_exemask  = KB_COUNT | KB_DECL | KB_EXPORTS | KB_STRUCT;

// Execution message enumerations
enum exemsg {
	EXE_OK,
	EXE_ARG2BIG,
	EXE_ARG2SML,
	EXE_CONFLICT,
	EXE_BADFORM,
	EXE_INVARG,
	EXE_NOFILE,
	EXE_NOTFOUND,
	EXE_2MANY,
};

// Fixed level enums for declaration types that have fixe levels
enum levels {
	LVL_FILE,
	LVL_EXPORTED,
	LVL_ARG,
	LVL_RETURN = LVL_ARG,
	LVL_NESTED,
};

static int kb_argmask =	((1 << EXE_ARG2BIG)  |
			 (1 << EXE_ARG2SML)  |
			 (1 << EXE_CONFLICT) |
			 (1 << EXE_BADFORM)  |
			 (1 << EXE_INVARG));

// Execution result messages
static const char *errstr[] = {
	[EXE_ARG2BIG]	= "Too many arguments",
	[EXE_ARG2SML]	= "Not enough arguments",
	[EXE_CONFLICT]	= "You entered conflicting switches",
	[EXE_BADFORM]	= "Badly formed argument list",
	[EXE_INVARG]    = "Invalid argument.",
	[EXE_NOFILE]	= "Seeking \"%s\", but cannot open database file %s\n",
	[EXE_NOTFOUND]	= "\"%s\" cannot be found in database file %s\n",
	[EXE_2MANY]	= "Too many items match \"%s\" in database %s."
			  " Be more specific.\n",
	"\0"
};

/*****************************************************
** global declarations
******************************************************/

static struct sqlite3 *db = NULL;
static char *kabitable = "kabitree";
static char *kabitypes = "kabitype";
enum exemsg;

static char *indent(int padsiz);
static void print_cmd_errmsg(enum exemsg err);
static void print_cmdline();
static int count_bits(unsigned mask);

/*****************************************************
** Database Access Utilities
******************************************************/

#define INTSIZ 32
#define PFXSIZ 16
#define DECLSIZ 256

enum rowtype {
	KB_TREE,
	KB_TYPE,
};

enum rowflags {
	ROW_NODUPS	= (1 << 0),
	ROW_ISDUP	= (1 << 1),
	ROW_DONE	= (1 << 2),
};

enum columns {
	COL_ROWID,
	COL_LEVEL,
	COL_LEFT,
	COL_RIGHT,
	COL_FLAGS,
	COL_PREFIX,
	COL_TYPE = COL_PREFIX,
	COL_DECL,
	COL_PARENTDECL,
	COL_OFFSET,
	COL_ROWTYPE,
	COL_ROWFLAGS,
};

struct row {
	char rowid[INTSIZ];
	union {
		char level[INTSIZ];
		int ilevel;
	};
	char left[INTSIZ];
	char right[INTSIZ];
	char flags[INTSIZ];
	union {
		char prefix[PFXSIZ];
		char type[DECLSIZ];
	};
	char decl[DECLSIZ];
	char parentdecl[DECLSIZ];
	int offset;
	enum rowtype rowtype;
	enum rowflags rowflags;
};

struct row *new_row(enum rowtype rowtype)
{
	struct row *pr = malloc(sizeof(struct row));
	memset(pr, 0, sizeof(struct row));
	pr->rowtype = rowtype;
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
	struct table *tptr = malloc(sizeof(struct table));
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

	if (drow->rowtype == KB_TREE)
		strncpy(drow->prefix, srow->prefix, PFXSIZ-1);
	else
		strncpy(drow->type, srow->type,     DECLSIZ-1);

	strncpy(drow->decl,       srow->decl,       DECLSIZ-1);
	strncpy(drow->parentdecl, srow->parentdecl, DECLSIZ-1);
}

/*****************************************************
** Duplicate Record Management
******************************************************/

enum dupman {
	DM_FILE,
	DM_EXPORTED,
	DM_ARG,
	DM_NESTED,
	DM_TBLSIZE,
};

static void init_signal_table(struct table *pt, char *decl)
{
	struct row *pr = pt->rows;
	int len = strlen(decl);
	int i;

	strncpy(pr[DM_NESTED].decl, decl, len);

	for (i = 0; i < DM_TBLSIZE; ++i)
		pr[i].rowtype = KB_TREE;
}

// test_dup - Returns true if the current record has the same decl field as
//            a previous one
//
static bool test_dup(struct table *pt, enum dupman dm, char *decl)
{
	struct row *pr = pt->rows;
	char *thisdecl = pr[dm].decl;
	int len = strlen(decl);

	if (strncmp(thisdecl, decl, len)) {
		memset(pr[dm].decl, 0, DECLSIZ);
		strncpy(pr[dm].decl, decl, len);
		pr[dm].rowflags &= ~ROW_ISDUP;
		return false;
	} else {
		pr[dm].rowflags |= ROW_ISDUP;
		return true;
	}
}

// is_dup - Compares the decl arg with the corresponding column in the
//          row in the signal_table indext by the dm parameter
//
static inline bool is_dup(struct table *pt, enum dupman dm)
{
	struct row *pr = pt->rows;
	return (pr[dm].rowflags & ROW_ISDUP) != 0;
}

static inline void clr_dup(struct table *pt, enum dupman dm)
{
	struct row *pr = pt->rows;
	pr[dm].rowflags &= ~ROW_ISDUP;
}

static void clr_dup_loop(struct table *pt, enum dupman min, enum dupman max)
{
	unsigned i;
	struct row *pr = pt->rows;
	for (i = min; i <= max; ++i)
		pr[i].rowflags &= ~ROW_ISDUP;
}

/*****************************************************
** Callbacks for SQLite execution of query statements
******************************************************/

int cb_process_field(void *output, int argc, char **argv, char **colnames)
{
	if (argc != 1 || !colnames)
		return -1;

	memcpy(output, argv[0], INTSIZ);

	return 0;
}

int cb_process_blob(void *array, int argc, char **argv, char **colnames)
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

// cb_process_row - callback to process one row from a select call
//
// NOTE: the "output" parameter, though cast as void * for the SQLite API,
//       must have been orignially created as struct row *, preferably
//       with a call to new_row().
//
int cb_process_row(void *output, int argc, char **argv, char **colnames)
{
	int i;
	int pfxsiz;
	struct row *pr;

	if (!argc || !colnames)
		return -1;

	pr = (struct row *)output;
	pfxsiz = pr->rowtype == KB_TYPE ? DECLSIZ-1 : PFXSIZ-1;

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
			strncpy(pr->prefix, argv[i], pfxsiz);
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


int cb_print_nv_struct2export(void *table,
			      int argc,
			      char **argv,
			      char **colnames)
{
	int level = (int)strtoul(argv[COL_LEVEL],0,0);;
	char *padstr = indent(level);
	struct table *pt = (struct table *)table;
	struct row *pr = pt->rows;

	if (!argc || !colnames)
		return -1;

	if ((level > LVL_FILE) && ((is_dup(pt, DM_EXPORTED) &&
				    is_dup(pt, DM_ARG)) ||
				  is_dup(pt, DM_NESTED)))
		return 0;

	switch (level) {
	case LVL_FILE	  :
		clr_dup_loop(pt, LVL_FILE, LVL_NESTED);
		return 0;

	case LVL_EXPORTED :
		if (!test_dup(pt, DM_FILE, argv[COL_PARENTDECL]))
			printf("FILE: %s\n", argv[COL_PARENTDECL]);

		if (!test_dup(pt, DM_EXPORTED, argv[COL_DECL]))
			printf("%s%s %s\n", padstr, argv[COL_PREFIX],
			       argv[COL_DECL]);
		break;

	case LVL_ARG	  :
		if (!(test_dup(pt, DM_ARG, argv[COL_DECL]) &&
				is_dup(pt, DM_EXPORTED)))
			printf("%s%s %s\n", padstr, argv[COL_PREFIX],
			       argv[COL_DECL]);
		break;

	default:
		if (test_dup(pt, DM_NESTED, argv[COL_DECL]))
			return 0;
		else {
			pr[DM_NESTED].ilevel = level;
			printf("%s%s\n", padstr, argv[COL_DECL]);
		}
		break;
	}

	return 0;
}

int cb_print_vb_struct2export(void *table,
			      int argc,
			      char **argv,
			      char **colnames)
{
	int level;
	char *padstr;
	struct table *pt = (struct table *)table;
	struct row *pr;

	if (!table || !argv || !argc || !colnames)
		return -1;

	level = (int)strtoul(argv[COL_LEVEL],0,0);
	padstr = indent(level);
	pr = pt->rows;

	if ((level > LVL_FILE) && ((is_dup(pt, DM_EXPORTED) &&
				    is_dup(pt, DM_ARG)) ||
				  is_dup(pt, DM_NESTED)))
		return 0;

	switch (level) {
	case LVL_FILE	  :
		clr_dup_loop(pt, LVL_FILE, LVL_NESTED);
		return 0;

	case LVL_EXPORTED :
		if (!test_dup(pt, DM_FILE, argv[COL_PARENTDECL]))
			printf("FILE: %s\n", argv[COL_PARENTDECL]);

		if (!test_dup(pt, DM_EXPORTED, argv[COL_DECL]))
			printf("%s%s %s\n", padstr, argv[COL_PREFIX],
			       argv[COL_DECL]);
		break;

	case LVL_ARG	  :
		if (!(test_dup(pt, DM_ARG, argv[COL_DECL]) &&
				is_dup(pt, DM_EXPORTED)))
			printf("%s%s %s\n", padstr, argv[COL_PREFIX],
			       argv[COL_DECL]);
		break;

	default:
		if (!test_dup(pt, DM_NESTED, argv[COL_DECL])) {
			pr[DM_NESTED].ilevel = level;
			printf("%s%s\n", padstr, argv[COL_DECL]);
		}
		break;
	}

	return 0;
}

// cb_print_row - callback to print one row from a select query
//
//       This function will print rows in the order in which they are
//       returned by the database query that called it, and only the
//       FILE, EXPORTED, and ARG prefixes will be printed.
//
int cb_print_row(void *table, int argc, char **argv, char **colnames)
{
	int level;
	char *padstr;
	struct table *pt = (struct table *)table;
	struct row *pr;
	bool dup = false;

	if (!argv || !argc || !colnames)
		return -1;

	level = (int)strtoul(argv[COL_LEVEL],0,0);

	if (table) {
		pr = pt->rows;
		level -= pr[DM_NESTED].offset;
		dup = test_dup(pt, DM_FILE, argv[COL_PARENTDECL]);
	}

	padstr = indent(level >= 0 ? level : 0);

	switch (level) {
	case LVL_FILE	  :
		return 0;

	case LVL_EXPORTED :
		if (!dup)
			printf("FILE: %s\n", argv[COL_PARENTDECL]);

	case LVL_ARG	  :
		printf("%s%s %s\n", padstr, argv[COL_PREFIX], argv[COL_DECL]);
		break;
	default		  :
		printf("%s%s\n", padstr, argv[COL_DECL]);
		break;
	}

	return 0;
}


/*****************************************************
** SQLite utilities and query wrappers
******************************************************/

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

// SQLite Query Strings
//
enum zstr{
	ZS_INNER,
	ZS_OUTER,
	ZS_INNER_LEVEL,
	ZS_OUTER_LEVEL,
	ZS_OUTER_VIEW,
	ZS_OUTER_VIEW_LEVEL,
	ZS_VIEWDECL_ANY,
	ZS_VIEWDECL_WORD,
	ZS_VIEWDECL_ANY_LEVEL,
	ZS_VIEWDECL_WORD_LEVEL,
	ZS_VIEW_INNER,
	ZS_VIEW_INNER_LEVEL,
	ZS_VIEW_LEVEL,
};

static char *zstmt[] = {
[ZS_INNER] =
	"select * from %q where left >= %lu AND right <= %lu",
[ZS_OUTER] =
	"select * from %q where left <= %lu AND right >= %lu",
[ZS_INNER_LEVEL] =
	"select * from %q where left >= %lu AND right <= %lu AND level %s",
[ZS_OUTER_LEVEL] =
	"select * from %q where left <= %lu AND right >= %lu AND level %s",
[ZS_OUTER_VIEW] =
	"select %q.* from %q,%q"
	" where %q.left <= %q.left and %q.right >= %q.right",
[ZS_OUTER_VIEW_LEVEL] =
	"select %q.* from %q,%q"
	" where %q.left <= %q.left and %q.right >= %q.right and %q.level %s",
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
[ZS_VIEW_INNER_LEVEL] =
	"create temp view %q as select * from %q where"
	" left >= %lu AND right <= %lu AND level %s",
[ZS_VIEW_LEVEL] =
	"create temp view %q as select from %q where level %s",
};


bool sql_concise_struct2export(char *table, char *vw, char *decl)
{
	bool rval;
	int (*cb)(void *, int, char ** ,char **)
			= decl ? cb_print_nv_struct2export : cb_print_row;
	struct table *pt = new_table(DM_TBLSIZE);
	char *zsql = sqlite3_mprintf(zstmt[ZS_OUTER_VIEW_LEVEL],
				     table, table, vw,
				     table, vw, table, vw, table, " == 1" );
	if (decl)
		init_signal_table(pt, decl);
	rval = sql_exec(zsql, cb, (void *)pt);
	sqlite3_free(zsql);
	delete_table(pt);
	return rval;
}

bool sql_verbose_struct2export(char *table, char *vw, char *decl)
{
	bool rval;
	int (*cb)(void *, int, char ** ,char **)
			= decl ? cb_print_vb_struct2export : cb_print_row;
	struct table *pt = new_table(DM_TBLSIZE);
	char *zsql = sqlite3_mprintf(zstmt[ZS_OUTER_VIEW],
				     table, table, vw,
				     table, vw, table, vw );
	if (decl)
		init_signal_table(pt, decl);
	rval = sql_exec(zsql, cb, (void *)pt);
	sqlite3_free(zsql);
	delete_table(pt);
	return rval;
}

bool sql_get_nest(char *table, char *vw, enum zstr zs)
{
	char *zsql = sqlite3_mprintf(zstmt[zs], table, table, vw,
				     table, vw, table, vw);
	bool rval = sql_exec(zsql, cb_print_row, NULL);
	sqlite3_free(zsql);
	return rval;
}


// sql_print_nest_view - create a view based on the nest boundaries
//                        can be inner or outer, depending on zs
//
// table - name of table or view from which to extract the nest
// left  - leftmost boundary of nest
// right - rightmost boundary of nest
// zs    - an enum that selects a command string from the zstmt array
// level - used if the selected zs needs it. Otherwise NULL to satisfy
//         the argument list.
// prow  - struct row
//
bool sql_print_nest_view(const char *view, long left, long right,
			  enum zstr zs, char *level, struct row *prow)
{
	bool rval;
	char *zsql;
	struct table *pt = new_table(DM_TBLSIZE);
	struct row *pr = pt->rows;

	if (level)
		zsql = sqlite3_mprintf(zstmt[zs], view, left, right, level);
	else
		zsql = sqlite3_mprintf(zstmt[zs], view, left, right);

	DBG(puts(zsql);)
	pr[DM_NESTED].offset = prow->offset;
	rval = sql_exec(zsql, cb_print_row, pt);
	sqlite3_free(zsql);
	delete_table(pt);
	return rval;
}

// sql_create_nest_view - create a view based on the nest boundaries
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
bool sql_create_nest_view(char *view, char *table,
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
//
bool sql_create_view_on_decl(char *view, char *table, char *declstr)
{
	bool rval;
	char *zsql;
	char *stmt;

	stmt = kb_flags & KB_WHOLE_WORD ?
			zstmt[ZS_VIEWDECL_WORD] :
			zstmt[ZS_VIEWDECL_ANY];
	zsql = sqlite3_mprintf(stmt, view, table, declstr);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, 0, 0);
	sqlite3_free(zsql);
	return rval;
}

bool sql_create_view_on_level(char *vwb, char *vwa, char *level)
{
	char *zsql = sqlite3_mprintf(zstmt[ZS_VIEW_LEVEL], vwb, vwa, level);
	bool rval = sql_exec(zsql, 0, 0);
	sqlite3_free(zsql);
	return rval;
}

bool sql_create_view_on_union(char *newview, char *viewA, char *viewB,
			      enum zstr zs)
{
	bool rval;
	char *stmt = zstmt[zs];
	char *zsql = sqlite3_mprintf(stmt, newview, viewA, viewB, viewB, viewB);

	DBG(puts(zsql);)
	rval = sql_exec(zsql, 0, 0);
	sqlite3_free(zsql);
	return rval;
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
	rval = sql_exec(zsql, cb_process_row, (void *)retrow);
	sqlite3_free(zsql);
	return rval;
}

// sql_exists - determines if a table or view already exists
//
// type - "view" or "table"
// name - the name assigned to the view or table when it was created
// temp - call with true if view was created as a temp
//
bool sql_exists(char *type, char *name, bool temp)
{
	char answer[INTSIZ];
	char *master = temp ? "sqlite_temp_master" : "sqlite_master";
	char *zsql = sqlite3_mprintf("select count() from %s where "
				     "type='%q' AND name='%q'",
				     master, type, name);

	sql_exec(zsql, cb_process_field, (void *)answer);
	sqlite3_free(zsql);

	if (!strcmp(answer, "0"))
		return false;

	return true;
}

bool sql_drop_view(char *view)
{
	char *zsql = sqlite3_mprintf("drop view %q", view);
	bool rval = sql_exec(zsql, 0, 0);
	sqlite3_free(zsql);
	return rval;
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
	rval = sql_exec(zsql, cb_process_field, (void *)count);
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
** String Manipulation
******************************************************/

// extract_words - extracts a number of words from one string to another
//
// sbuf  - destination string
// str   - source string (haystack)
// seek  - first word we're seeking (needle)
// delim - what delimits the words
// qty   - how many we want, including first one, after finding first one.
// size  - destination buffer size
//
static bool extract_words(char *sbuf, char *str, char *seek,
			  char *delim, int qty, int size)
{
	bool found = false;
	int count = 0;
	char *c = strtok(str, delim);

	memset(sbuf, '\0', size);

	do {
		if (!strcmp(c, seek))
			found = true;

		if (found) {
			strcat(sbuf, c);
			strcat(sbuf, " ");
			++count;
		}

	} while ((c = strtok(NULL, delim)) && (count < qty));

	if (count < qty)
		return false;

	return true;
}

// tokenize - tokenizes a string into substrings divided by spaces
//
// sbuf - user's buffer containing the string to be tokenized
//
// Returns a dynamically allocated array of strings. It is the
// caller's responsibility to free this pointer when done with it.
//
char **tokenize(char *sbuf)
{
	char *dup = strdup(sbuf);
	char *c = dup;
	int words = 0;
	char **strarray;

	while( *(++c))
		if (isspace(*c))
			++words;

	strarray = (char **)malloc(words * sizeof(char *));
	c = strtok(dup, " ");

	do {
		*(strarray++) = c;
	} while ((c = strtok(NULL, " ")));

	free(dup);
	return strarray;
}

static char *trim_trail(char *str)
{
	unsigned len = strlen(str);
	char *c = str + len;

	while (isspace(*(--c)))
		*c = 0;

	return str;
}

// is_exact - Look for an exact word match
//
// Returns true if the user's input string exactly matches a string in
// the decl field of the kabi database. If user is looking for
// "struct device", will return false on "struct device_private", whereas
// the sqlite database will see these as equivalent enough for a match.
//
// declstr - haystack, the data returned by sqlite
// needle  - user's input from the console
//
static bool is_exact(char *haystack, char *needle)
{
	unsigned len = strlen(needle);

	if ((strlen(haystack) == len) && (!strcmp(haystack, needle)))
		return true;

	return false;
}

// check_declstr - looking for one exact match among a number of possibilities
//
// count   - the number of hits sqlite got from the user's input
// declstr - the user's input from the console
// view    - the sqlite view or table where the hits were made
// prow    - pointer to a row structure being used to obtain the data
//
static bool check_declstr(int count, char *declstr,
			  char *view, struct row *prow)
{
	int i;
	char sbuf[DECLSIZ];
	char *dup = strdup(prow->decl);

	extract_words(sbuf, dup, declstr, " ", 1, DECLSIZ);
	trim_trail(sbuf);

	for (i = 0; i < count; ++i) {

		if (is_exact(sbuf, declstr))
			return true;

		puts(prow->decl);
		memset(prow, 0, (sizeof(struct row)));
		sql_get_one_row(view, i, prow);
	}
	free(dup);
	return false;
}

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

/*****************************************************
** Mine the database and Format the Results
******************************************************/

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

	sscanf(prow->left,  "%lu", &left);
	sscanf(prow->right, "%lu", &right);

	sql_print_nest_view(kabitable, left, right, zs, level, prow);
	return true;
}

static bool get_struct(struct row *prow)
{
	char *view = "sv";
	char *str = strdup(prow->decl);
	char sbuf[DECLSIZ];
	int level;
	struct row *pr;

	if (sql_exists("view", "sv", true))
		sql_exec("drop view sv", 0, 0);

	extract_words(sbuf, str, "struct", " ", 2, DECLSIZ);
	sql_create_view_on_decl(view, kabitypes, sbuf);

	pr = new_row(KB_TYPE);
	sql_get_one_row(view, 0, pr);
	sscanf(pr->level, "%d", &level);
	pr->offset = level - 3;
	strncpy(pr->type, prow->decl, DECLSIZ);
	process_row(pr, ZS_INNER, NULL);

	free(str);
	delete_row(pr);
	sql_exec("drop view sv", 0, 0);
	return true;
}

static void get_verbose_struct2exports(char *vw, char *declstr)
{
	if (kb_flags & KB_NODUPS)
		sql_verbose_struct2export(kabitable, vw, declstr);
	else
		sql_verbose_struct2export(kabitable, vw, NULL);
}

static void get_concise_struct2exports(char *vw, char *declstr)
{
	if (kb_flags & KB_NODUPS)
		sql_concise_struct2export(kabitable, vw, declstr);
	else
		sql_concise_struct2export(kabitable, vw, NULL);
}

static int exe_struct(char *declstr, char *datafile)
{
	char *vwa = "vwa";

	if (!sql_open(datafile))
		return EXE_NOFILE;

	sql_create_view_on_decl(vwa, kabitable, declstr);

	if (kb_flags & KB_VERBOSE)
		get_verbose_struct2exports(vwa, declstr);
	else
		get_concise_struct2exports(vwa, declstr);

	sql_drop_view(vwa);
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

	sql_create_view_on_decl(view, kabitable, declstr);
	get_count(view, &count);

	if (count < 1)  {
		rval = EXE_NOTFOUND;
		goto out;
	}

	prow = new_row(KB_TREE);
	sql_get_one_row(view, 0, prow);

	if ((count > 1) && (!check_declstr(count, declstr, view, prow))) {
		rval = EXE_2MANY;
		goto out;
	}

	if (kb_flags & KB_VERBOSE) {
		int i;
		long left;
		long right;

		sscanf(prow->left,  "%lu", &left);
		sscanf(prow->right, "%lu", &right);
		process_row(prow, ZS_OUTER_LEVEL, "== 1");

		sql_exec("drop view kt", 0 , 0);
		sql_create_nest_view(view, kabitable, left, right,
					ZS_VIEW_INNER_LEVEL, "<= 2");
		get_count(view, &count);

		for (i = 1; i < count; ++i) {
			memset(prow, 0, sizeof(struct row));
			sql_get_one_row(view, i, prow);
			process_row(prow, ZS_OUTER_LEVEL, "== 2");

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

	prow = new_row(KB_TREE);
	sql_create_view_on_decl(view, kabitable, declstr);
	get_count(view, &count);
	printf("%d\n", count);
	delete_row(prow);
	sql_exec("drop view kt", 0, 0);
	sql_close(db);
	return EXE_OK;
}

static int exe_decl(char *declstr, char *datafile)
{
	int rval;
	int level;
	int count;
	char *view = "st";
	char lvlstr[INTSIZ];
	struct row *prow;

	if (!sql_open(datafile))
		return EXE_NOFILE;

	if (sql_exists("view", "st", true))
		sql_exec("drop view kt", 0, 0);

	sql_create_view_on_decl(view, kabitypes, declstr);
	get_count(view, &count);

	if (count < 1)  {
		rval = EXE_NOTFOUND;
		goto out;
	}

	prow = new_row(KB_TYPE);
	sql_get_one_row(view, 0, prow);
	sscanf(prow->level, "%d", &level);

	if (level < LVL_ARG) {
		rval = EXE_INVARG;
		goto out;
	}

	sprintf(lvlstr, "<= %d", level + 1);
	prow->offset = level - LVL_NESTED;

	if (kb_flags & KB_VERBOSE)
		process_row(prow, ZS_INNER, NULL);
	else
		process_row(prow, ZS_INNER_LEVEL, lvlstr);

	rval = EXE_OK;
out:
	delete_row(prow);
	sql_exec("drop view st", 0, 0);
	sql_close(db);
	return rval;
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

enum longopt {
	OPT_NODUPS,
	OPT_ARGS,
};

char *longopts[] = {
	[OPT_NODUPS] = "no-dups",
	[OPT_ARGS] = "args",
};

static bool parse_long_opt(char *argstr)
{
	unsigned i;

	for (i = 0; i < sizeof((long)*longopts)/sizeof(char *); ++i)
		if(!strcmp(++argstr, longopts[i]))
			break;
	switch (i) {
	case OPT_NODUPS :
		kb_flags |= KB_NODUPS;
		break;
	case OPT_ARGS	:
		kb_flags |= KB_ARGS;
		break;
	default		:
		return false;
	}

	return true;
}

static bool get_options(char **argv, int *idx)
{
	int index = 0;
	char *argstr;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		argstr = &(*argv++)[1];

		if (*argstr == '-')
			if(parse_long_opt(argstr))
				continue;

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
