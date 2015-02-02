;/* kabi.c
 *
 * Find all the exported symbols in .i file(s) passed as argument(s) on the
 * command line when invoking this executable.
 *
 * Relies heavily on the sparse project. See
 * https://www.openhub.net/p/sparse Include files for sparse in Fedora are
 * located in /usr/include/sparse when sparse is built and installed.
 *
 * Method for identifying exported symbols was inspired by the spartakus
 * project:
 * http://git.engineering.redhat.com/git/users/sbairagy/spartakus.git
 *
 * 1. Gather all the exported symbols
 * 2. Gather all the args of the exported symbols.
 * 3. Recursively descend into the compound types in the arg lists to
 *    acquire all information on compound types that can affect the
 *    exported symbols.
 *
 * Only unique symbols will be listed, rather than listing them everywhere
 * they are discovered.
 *
 * What is a symbol? From sparse/symbol.h:
 *
 * 	An identifier with semantic meaning is a "symbol".
 *
 * 	There's a 1:n relationship: each symbol is always
 * 	associated with one identifier, while each identifier
 * 	can have one or more semantic meanings due to C scope
 * 	rules.
 *
 * 	The progression is symbol -> token -> identifier. The
 * 	token contains the information on where the symbol was
 * 	declared.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm-generic/errno-base.h>
#define NDEBUG	// comment out to enable asserts
#include <assert.h>

#include <sparse/lib.h>
#include <sparse/allocate.h>
#include <sparse/parse.h>
#include <sparse/symbol.h>
#include <sparse/expression.h>
#include <sparse/token.h>
#include <sparse/ptrlist.h>
#include <sqlite3.h>

#define STD_SIGNED(mask, bit) (mask == (MOD_SIGNED | bit))
#define STRBUFSIZ 256

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
kabi [options] filespec\n\
\n\
Parses \".i\" (intermediate, c-preprocessed) files for exported symbols and\n\
symbols of structs and unions that are used by the exported symbols.\n\
\n\
    filespec  File or files (wildcards ok) to be processed\n\
\n\
Options:\n\
    -k        Name of the database file to write or update.\n\
              Defaut is \"../kabitree.sql\"\n\
\n\
    -v        Verbose (default): Lists all the arguments of functions and\n\
              recursively descends into compound types to gather all the\n\
              information about them.\n\
\n\
    -v-       Concise: Lists only the exported functions and their args.\n\
\n\
    -u        Lists users of every compound type\n\
\n\
    -u-       Turn off lists of users (default)\n\
\n\
    -h        This help message.\n\
\n";

static const char *ksymprefix = "__ksymtab_";
static bool kp_verbose = true;
static bool showusers = false;
DBG(static int hiwater = 0;)
static struct symbol_list *symlist = NULL;


/*****************************************************
** global sqlite3 data
******************************************************/

const char *kabi_table_schema = "\
key integer primary key, \
id integer, \
parentid integer, \
level int, \
flags int, \
prefix text, \
decl text, \
parentdecl text";

static const char *kabi_table_name = "kabitree";
static char *kabi_sql_filename = NULL;

/*****************************************************
** enumerated flags and masks
******************************************************/

enum typemask {
	SM_UNINITIALIZED = 1 << SYM_UNINITIALIZED,
	SM_PREPROCESSOR  = 1 << SYM_PREPROCESSOR,
	SM_BASETYPE	 = 1 << SYM_BASETYPE,
	SM_NODE		 = 1 << SYM_NODE,
	SM_PTR		 = 1 << SYM_PTR,
	SM_FN		 = 1 << SYM_FN,
	SM_ARRAY	 = 1 << SYM_ARRAY,
	SM_STRUCT	 = 1 << SYM_STRUCT,
	SM_UNION	 = 1 << SYM_UNION,
	SM_ENUM		 = 1 << SYM_ENUM,
	SM_TYPEDEF	 = 1 << SYM_TYPEDEF,
	SM_TYPEOF	 = 1 << SYM_TYPEOF,
	SM_MEMBER	 = 1 << SYM_MEMBER,
	SM_BITFIELD	 = 1 << SYM_BITFIELD,
	SM_LABEL	 = 1 << SYM_LABEL,
	SM_RESTRICT	 = 1 << SYM_RESTRICT,
	SM_FOULED	 = 1 << SYM_FOULED,
	SM_KEYWORD	 = 1 << SYM_KEYWORD,
	SM_BAD		 = 1 << SYM_BAD,
};

enum ctlflags {
	CTL_POINTER 	= 1 << 0,
	CTL_ARRAY	= 1 << 1,
	CTL_STRUCT	= 1 << 2,
	CTL_FUNCTION	= 1 << 3,
	CTL_EXPORTED 	= 1 << 4,
	CTL_RETURN	= 1 << 5,
	CTL_ARG		= 1 << 6,
	CTL_NESTED	= 1 << 7,
	CTL_GODEEP	= 1 << 8,
	CTL_DONE	= 1 << 9,
	CTL_FILE	= 1 << 10,
};

/*****************************************************
** sparse wrappers
******************************************************/

static inline void add_string(struct string_list **list, char *string)
{
	// Need to use "notag" call, because bits[1:0] are reserved
	// for tags of some sort. Pointers to strings do not necessarily
	// have the low two bits clear, so this "notag" call is provided
	// for that situation.
	add_ptr_list_notag(list, string);
}

static inline int string_list_size(struct string_list *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

/*****************************************************
** SQLite utilities and wrappers
******************************************************/
// if this were c++, db and table would be private members
// of a sql class, accessible only by member function calls.
// Move these utilities and wrappers into their own file,
// with their own header to achieve the encapsulation.
static struct sqlite3 *db = NULL;
static sqlite3_stmt *stmt_row;
static sqlite3_stmt *stmt_decl;
static sqlite3_stmt *stmt_parentdecl;

inline struct sqlite3 *sql_get_db()
{
	return db;
}

static char *table;
// initialize the global name of the table
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

bool sql_extract_field(char *field, void *id, char *output)
{
	char *errmsg;
	char *zsql = sqlite3_mprintf("select %q from %q where id==%d",
				     field, sql_get_table(), id);

	int retval = sqlite3_exec
			(db, zsql, sql_process_field, (void *)output, &errmsg);

	if (retval != SQLITE_OK) {
		fprintf(stderr,
			"\nError in statement: %s [%s].\n", zsql, errmsg);
		sqlite3_free(errmsg);
		return false;
	}
	return true;
}

bool sql_prepare_update_field
		(sqlite3_stmt **stmt, char *field, char *val)
{
	char *zsql;
	zsql = sqlite3_mprintf("update %q set %q=%q where id==:id",
			       sql_get_table(), field, val);
	int retval = sqlite3_prepare_v2(db, zsql, strlen(zsql), stmt, 0);
	if (retval != SQLITE_OK)
		fprintf(stderr, "Could not prepare update requested field. "
			"[%s]\n", sqlite3_errstr(retval));
	sqlite3_free(zsql);
	return retval == SQLITE_OK;
}

bool sql_step(sqlite3_stmt *stmt)
{
	int retval = sqlite3_step(stmt);

	if(retval != SQLITE_DONE) {
		fprintf (stderr, "Could not step statement. [%s]\n",
			 sqlite3_errstr(retval));
		return false;
	}
	return true;
}

inline void sql_reset(sqlite3_stmt *stmt)
{
	sqlite3_reset(stmt);
}

inline bool sql_finalize(sqlite3_stmt *stmt)
{
	return sqlite3_finalize(stmt) == SQLITE_OK;
}

// sql_init, if successful, initializes the global sqlite3 *db
// Send table schema as a string of comma separated fieldnames paired
// with their data type, e.g.
// "field1 txt,field2 integer"
bool sql_init(const char *sqlfilename,
	      const char *table_schema,
	      const char *table_name)
{
	char *zsql;
	int rval;

	rval = sqlite3_open(sqlfilename, &db);

	if (rval != SQLITE_OK) {
		fprintf(stderr, "Unable to create database: %s\n", sqlfilename);
		return false;
	}

	fprintf(stderr, "Created new database file: %s\n", sqlfilename);

	zsql = sqlite3_mprintf("create table %q(%q)",
			       table_name, table_schema);
	if (!sql_exec(zsql))
		return false;

	sql_set_table(table_name);
	fprintf(stderr, "Created new table: %s\n", table_name);
	fprintf(stderr, "Schema: %s\n", table_schema);
	sqlite3_free(zsql);
	return sql_exec("pragma synchronous = off;"
			"pragma journal_mode = off;");
}

bool sql_open(const char *sqlfilename, const char *tablename)
{
	int rval = sqlite3_open_v2
			(sqlfilename, &db, SQLITE_OPEN_READWRITE, NULL);

	if (rval != SQLITE_OK) {
		fprintf(stderr, "sqlite3_open_v2() returned %s\n",
			sqlite3_errstr(rval));
		return false;
	}
	sql_set_table(tablename);
	return true;
}

inline void sql_close(sqlite3 *db)
{
	sqlite3_close(db);
}

static void sql_prepare_kabi_stmts()
{
	char *zsql;
	int len;
	int retval;
	zsql = sqlite3_mprintf
			("insert into %q (id,parentid,level,flags,prefix) "
			 "values (:id,:parentid,:level,:flags,:prefix)",
			 sql_get_table());
	len = strlen(zsql);
	retval = sqlite3_prepare_v2(db, zsql, len, &stmt_row, 0);

	if (retval != SQLITE_OK)
		fprintf(stderr, "Could not prepare stmt_row. "
			"[%s]\n", sqlite3_errstr(retval));

	sqlite3_free(zsql);
	sql_prepare_update_field(&stmt_decl, "decl", ":decl");
	sql_prepare_update_field(&stmt_parentdecl, "parentdecl", ":parentdecl");
}

static void sql_finalize_kabi_stmts()
{
	sql_finalize(stmt_row);
	sql_finalize(stmt_decl);
	sql_finalize(stmt_parentdecl);
}

/*****************************************************
** knode declaration and utilities
******************************************************/

struct knode {
	struct knode *parent;
	struct knodelist *children;
	struct symbol *symbol;
	struct symbol_list *symbol_list;
	struct string_list *declist;
	char *name;
	char *typnam;
	char *file;
	enum ctlflags flags;
	int level;
};

DECLARE_PTR_LIST(knodelist, struct knode);
static struct knodelist *knodes = NULL;

static struct knode *alloc_knode()
{
	int knsize = sizeof(struct knode);
	struct knode *kptr;

	if(!(kptr = (struct knode *)malloc(knsize)))
		return NULL;

	memset(kptr, 0, knsize);
	return kptr;
}

static inline void add_knode (struct knodelist **klist, struct knode *k)
{
	add_ptr_list(klist, k);
}

static inline struct knode *last_knode(struct knodelist *head)
{
	return last_ptr_list((struct ptr_list *)head);
}

static inline int knode_list_size(struct knodelist *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static struct knode *map_knode
		(struct knode *parent,
		 struct symbol *symbol,
		 enum ctlflags flags)
{
	struct knode *newknode;
	newknode = alloc_knode();
	newknode->parent = parent;
	newknode->file = parent->file;
	newknode->flags |= flags;
	newknode->symbol = symbol;
	if (flags & CTL_NESTED)
		newknode->level = parent->level + 1;
	add_knode(&parent->children, newknode);
	DBG(hiwater= newknode->level > hiwater ? newknode->level : hiwater;)
	return newknode;
}

/*****************************************************
** used declaration and utilities
******************************************************/
struct used {
	struct symbol *symbol;
	struct knodelist *users;
};

DECLARE_PTR_LIST(usedlist, struct used);
static struct usedlist *redlist = NULL;

static struct used *alloc_used()
{
	int usize = sizeof(struct used);
	struct used *uptr;

	if(!(uptr = (struct used *)malloc(usize)))
		return NULL;

	memset(uptr, 0, usize);
	return uptr;
}

static inline void add_used(struct usedlist **list, struct used *u)
{
	add_ptr_list(list, u);
}

static struct used *lookup_used(struct usedlist *list, struct symbol *sym)
{
	struct used *temp;
	FOR_EACH_PTR(list, temp) {
		if (temp->symbol == sym)
			return temp;
	} END_FOR_EACH_PTR(temp);
	return NULL;
}

static void add_new_used
		(struct usedlist **list,
		 struct knode *parent,
		 struct symbol *sym)
{
	struct used *newused = alloc_used();
	newused->symbol = sym;
	add_knode(&newused->users, parent);
	add_used(list, newused);
}

static bool add_to_used
		(struct usedlist **list,
		 struct knode *parent,
		 struct symbol *sym)
{
	struct used *user;

	if ((user = lookup_used(*list, sym))) {
		add_knode(&user->users, parent);
		return true;
	} else {
		add_new_used(list, parent, sym);
		return false;
	}
}

/*****************************************************
** Output formatting
******************************************************/

static void compose_declaration(struct knode *kn, char *sbuf)
{
	char *str;

	memset(sbuf, 0, STRBUFSIZ);

	FOR_EACH_PTR_NOTAG(kn->declist, str) {
		strcat(sbuf, str);
		strcat(sbuf, " ");
	} END_FOR_EACH_PTR_NOTAG(str);

	if (kn->flags & CTL_POINTER)
		strcat(sbuf, "*");

	if (kn->name)
		strcat(sbuf, kn->name);
}

static void format_declaration(struct knode *kn)
{
	int idx;
	char *sbuf = calloc(STRBUFSIZ, 1);
	struct knode *parent = kn->parent;

	compose_declaration(kn, sbuf);
	printf("%s ", sbuf);

	idx = sqlite3_bind_parameter_index(stmt_decl, ":decl");
	sqlite3_bind_text(stmt_decl, idx, sbuf, strlen(sbuf), 0);
	idx = sqlite3_bind_parameter_index(stmt_decl, ":id");
	sqlite3_bind_int(stmt_decl, idx, (long int)kn);
	sql_step(stmt_decl);
	sql_reset(stmt_decl);

	if (!parent)
		goto out;

	if ((kn == parent)
	|| (parent->flags & CTL_FILE))
		goto out;

	if (parent->declist) {
		compose_declaration(parent, sbuf);
		printf ("IN: %s ", sbuf);

		idx = sqlite3_bind_parameter_index
				(stmt_parentdecl, ":parentdecl");
		sqlite3_bind_text(stmt_parentdecl, idx, sbuf, strlen(sbuf), 0);
		idx = sqlite3_bind_parameter_index(stmt_parentdecl, ":id");
		sqlite3_bind_int(stmt_parentdecl, idx, (long int)kn);
		sql_step(stmt_parentdecl);
		sql_reset(stmt_parentdecl);
	}
out:
	free(sbuf);
	putchar('\n');
}

static char *pad_out(int padsize, char padchar)
{
	static char buf[BUFSIZ];
	memset(buf, 0, BUFSIZ);
	while(padsize--)
		buf[padsize] = padchar;
	return buf;
}

/*****************************************************
** sparse probing and mining
******************************************************/

static struct symbol *find_internal_exported(struct symbol_list *, char *);
static bool starts_with(const char *a, const char *b);
static const char *get_modstr(unsigned long mod);
static void get_declist(struct knode *kn, struct symbol *sym);
static void get_symbols
		(struct knode *parent,
		 struct symbol_list *list,
		 enum ctlflags flags);

static void proc_symlist
		(struct knode *parent,
		 struct symbol_list *list,
		 enum ctlflags flags)
{
	struct symbol *sym;
	FOR_EACH_PTR(list, sym) {
		get_symbols(parent, sym->symbol_list, flags);
	} END_FOR_EACH_PTR(sym);
}

static void get_declist(struct knode *kn, struct symbol *sym)
{
	struct symbol *basetype = sym->ctype.base_type;
	const char *typnam = "\0";

	if (! basetype)
		return;

	if (basetype->type) {
		enum typemask tm = 1 << basetype->type;
		typnam = get_type_name(basetype->type);

		if (basetype->type == SYM_BASETYPE) {
			if (! basetype->ctype.modifiers)
				typnam = "void";
			else
				typnam = get_modstr(basetype->ctype.modifiers);
		}

		if (basetype->type == SYM_PTR)
			kn->flags |= CTL_POINTER;
		else
			add_string(&kn->declist, (char *)typnam);

		if ((tm & (SM_STRUCT | SM_UNION))
		&& (basetype->symbol_list)) {
			add_symbol(&kn->symbol_list, basetype);
			kn->flags |= CTL_STRUCT;
		}

		if (tm & SM_FN)
			kn->flags |= CTL_FUNCTION;
	}

	if (basetype->ident) {
		kn->typnam = basetype->ident->name;
		add_string(&kn->declist, basetype->ident->name);
	} else
		kn->typnam = (char *)typnam;

	get_declist(kn, basetype);
}

static void get_symbols
		(struct knode *parent,
		 struct symbol_list *list,
		 enum ctlflags flags)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct knode *kn;
		struct symbol *basetype = sym->ctype.base_type;
		enum typemask tm;

		if (basetype)
			tm = 1 << basetype->type;

		if (parent->symbol == sym)
			return;

		if ((tm & (SM_STRUCT | SM_UNION))
				&& add_to_used(&redlist, parent, sym))
			return;

		kn = map_knode(parent, sym, flags);
		get_declist(kn, sym);

		if (sym->ident)
			kn->name = sym->ident->name;

		// Avoid redundancies as well as the possibility of infinite
		// recursion. Infinite recursion most often occurs when a
		// struct has members that point back to itself, as in ..
		// struct list_head {
		// 	struct list_head *next;
		// 	struct list_head *prev;
		// };
		if (!(add_to_used(&redlist, parent, sym)))
			proc_symlist(kn, kn->symbol_list, CTL_NESTED);

	} END_FOR_EACH_PTR(sym);
}

static void build_branch(struct knode *parent, char *symname)
{
	struct symbol *sym;

	if ((sym = find_internal_exported(symlist, symname))) {
		struct symbol *basetype = sym->ctype.base_type;
		struct knode *kn = map_knode(parent, NULL, CTL_EXPORTED);

		kn->name = symname;
		get_declist(kn, sym);

		if (kn->symbol_list) {
			struct knode *bkn = map_knode(kn, basetype, CTL_RETURN);
			get_declist(bkn, basetype);
		}

		if (basetype->arguments)
			get_symbols(kn, basetype->arguments, CTL_ARG);
	}
}

static bool starts_with(const char *a, const char *b)
{
	if(strncmp(a, b, strlen(b)) == 0)
		return true;

	return false;
}

static void build_tree(struct symbol_list *symlist, struct knode *parent)
{
	struct symbol *sym;

	FOR_EACH_PTR(symlist, sym) {

		if (sym->ident &&
		    starts_with(sym->ident->name, ksymprefix)) {
			int offset = strlen(ksymprefix);
			char *symname = &sym->ident->name[offset];
			build_branch(parent, symname);
		}

	} END_FOR_EACH_PTR(sym);
}

static const char *get_modstr(unsigned long mod)
{
	static char buffer[100];
	unsigned len = 0;
	int bits = 0;
	unsigned i;
	struct mod_name {
		unsigned long mod;
		const char *name;
	} *m;

	static struct mod_name mod_names[] = {
		{MOD_AUTO,		"auto"},
		{MOD_REGISTER,		"register"},
		{MOD_STATIC,		"static"},
		{MOD_EXTERN,		"extern"},
		{MOD_CONST,		"const"},
		{MOD_VOLATILE,		"volatile"},
		{MOD_SIGNED,		"signed"},
		{MOD_UNSIGNED,		"unsigned"},
		{MOD_CHAR,		"char"},
		{MOD_SHORT,		"short"},
		{MOD_LONG,		"long"},
		{MOD_LONGLONG,		"long long"},
		{MOD_LONGLONGLONG,	"long long long"},
		{MOD_TYPEDEF,		"typedef"},
		{MOD_TLS,		"tls"},
		{MOD_INLINE,		"inline"},
		{MOD_ADDRESSABLE,	"addressable"},
		{MOD_NOCAST,		"nocast"},
		{MOD_NODEREF,		"noderef"},
		{MOD_ACCESSED,		"accessed"},
		{MOD_TOPLEVEL,		"toplevel"},
		{MOD_ASSIGNED,		"assigned"},
		{MOD_TYPE,		"type"},
		{MOD_SAFE,		"safe"},
		{MOD_USERTYPE,		"usertype"},
		{MOD_NORETURN,		"noreturn"},
		{MOD_EXPLICITLY_SIGNED,	"explicitly-signed"},
		{MOD_BITWISE,		"bitwise"},
		{MOD_PURE,		"pure"},
	};

	// Return these type modifiers the way we're accustomed to seeing
	// them in the source code.
	//
	// reported by sparse/show-parse.c::modifier_string()  as ...
	//
	if (mod == MOD_SIGNED)
		return "int";			// [signed]
	if (mod == MOD_UNSIGNED)
		return "unsigned int";		// [unsigned]
	if (STD_SIGNED(mod, MOD_CHAR))
		return "char";			// [signed][char]
	if (STD_SIGNED(mod, MOD_LONG))
		return "long";			// [signed][long]
	if (STD_SIGNED(mod, MOD_LONGLONG))
		return "long long";		// [signed][long long]
	if (STD_SIGNED(mod, MOD_LONGLONGLONG))
		return "long long long";	// [signed][long long long]

	// More than one of these bits can be set at the same time,
	// so clear the redundant ones.
	//
	if ((mod & MOD_LONGLONGLONG) && (mod & MOD_LONGLONG))
		mod &= ~MOD_LONGLONG;
	if ((mod & MOD_LONGLONGLONG) && (mod & MOD_LONG))
		mod &= ~MOD_LONG;
	if ((mod & MOD_LONGLONG) && (mod & MOD_LONG))
		mod &= ~MOD_LONG;

	// Now scan the list for matching bits and copy the corresponding
	// names into the return buffer.
	//
	for (i = 0; i < ARRAY_SIZE(mod_names); i++) {
		m = mod_names + i;
		if (mod & m->mod) {
			char c;
			const char *name = m->name;
			while ((c = *name++) != '\0' && len + 1 < sizeof buffer)
				buffer[len++] = c;
			buffer[len++] = ' ';
			bits++;
		}
	}
	if (bits > 1)
		buffer[len-1] = 0;
	else
		buffer[len] = 0;
	return buffer;
}

// find_internal_exported (symbol_list* symlist, char *symname)
//
// Finds the internal declaration of an exported symbol in the symlist.
// The symname parameter must have the "__ksymtab_" prefix stripped from
// the exported symbol name.
//
// symlist - pointer to a list of symbols
//
// symname - the ident->name string of the exported symbol
//
// Returns a pointer to the symbol that corresponds to the exported one.
//
static struct symbol *find_internal_exported
				(struct symbol_list *symlist,
				 char *symname)
{
	struct symbol *sym = NULL;

	FOR_EACH_PTR(symlist, sym) {

		if (!(sym && sym->ident))
			continue;

		if (! strcmp(sym->ident->name, symname)) {
			switch (sym->ctype.base_type->type) {
			case SYM_BASETYPE:
			case SYM_PTR:
			case SYM_FN:
			case SYM_ARRAY:
			case SYM_STRUCT:
			case SYM_UNION:
				goto fie_foundit;
			}
		}
	} END_FOR_EACH_PTR(sym);

fie_foundit:
	return sym;
}

/*****************************************************
** Output utilities
******************************************************/

#if !defined(NDEBUG)	// see /usr/include/assert.h
static int count_bits(unsigned long mask)
{
	int count = 0;

	do {
		count += mask & 1;
	} while (mask >>= 1);

	return count;
}
#endif

static char *get_prefix(enum ctlflags flags)
{
	// These flags are mutually exclusive.
	//
	unsigned long flg = flags &
			(CTL_FILE 	|
			 CTL_EXPORTED 	|
			 CTL_RETURN	|
			 CTL_ARG 	|
			 CTL_NESTED);

	assert(count_bits(flg) <= 1);

	switch (flg) {
	case CTL_FILE:
		return "FILE: ";
	case CTL_EXPORTED:
		return "EXPORTED: ";
	case CTL_RETURN:
		return "RETURN: ";
	case CTL_ARG:
		return "ARG: ";
	case CTL_NESTED:
		return "NESTED: ";
	}
	return "";
}

static void show_users(struct knodelist *klist, int level)
{
	struct knode *kn;

	FOR_EACH_PTR(klist, kn) {

		if (kn->flags & CTL_STRUCT) {
			printf("%s USER: ", pad_out(level+10, ' '));
			format_declaration(kn);
		}
	}END_FOR_EACH_PTR(kn);
}

static void show_knodes(struct knodelist *klist)
{
	struct knode *kn;

	FOR_EACH_PTR(klist, kn) {
		char *pfx = get_prefix(kn->flags);
		int idx;

		// Top of the tree has no name, just descendants
		if (kn->parent == kn)
			goto nextlevel;

		if (!kp_verbose && kn->flags & CTL_NESTED)
			return;

		idx = sqlite3_bind_parameter_index(stmt_row, ":id");
		sqlite3_bind_int(stmt_row, idx, (long int)kn);
		idx = sqlite3_bind_parameter_index(stmt_row, ":parentid");
		sqlite3_bind_int(stmt_row, idx, (long int)kn->parent);
		idx = sqlite3_bind_parameter_index(stmt_row, ":level");
		sqlite3_bind_int(stmt_row, idx, kn->level);
		idx = sqlite3_bind_parameter_index(stmt_row, ":flags");
		sqlite3_bind_int(stmt_row, idx, kn->flags);
		idx = sqlite3_bind_parameter_index(stmt_row, ":prefix");
		sqlite3_bind_text(stmt_row, idx, pfx, strlen(pfx), 0);
		sql_step(stmt_row);
		sql_reset(stmt_row);

		if ((kp_verbose) && (kn->flags & CTL_NESTED))
			printf("%s%s%-2d ",
				pad_out(kn->level, ' ' ), pfx, kn->level);
		else
			printf("%s%s ", pad_out(kn->level, ' ' ), pfx);

		format_declaration(kn);

		if (showusers && (kn->flags & CTL_NESTED)) {
			struct used *u = lookup_used(redlist, kn->symbol);
			if (u)
				show_users(u->users, kn->level);
		}
nextlevel:
		if (kn->children) {
			show_knodes(kn->children);
		}

	} END_FOR_EACH_PTR(kn);
}

/*****************************************************
** Command line option parsing
******************************************************/

static int parse_opt(char opt, int state, char *argv)
{
	int optstatus = 1;

	switch (opt) {
	case 'v' : kp_verbose = state;	// kp_verbose is global to this file
		   break;
	case 'u' : showusers = state;
		   break;
	case 'k' : kabi_sql_filename = argv;
		   optstatus = 2;
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
	int optstatus = 0;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		char *argstr = &(*argv++)[1];

		// Trailing '-' sets state to OFF (0) for switches.
		state = argstr[strlen(argstr)-1] != '-';

		for (i = 0; argstr[i]; ++i)
			optstatus = parse_opt(argstr[i], state, *argv);

		// If we consumed the next string in the argument,
		// advance the argument pointer and the index,
		// because we consumed two args: the switch that
		// indicated another string to consume and the
		// string we consumed.
		if(optstatus == 2) {
			++argv;
			++index;
		}
	}
	return index;
}

/*****************************************************
** main
******************************************************/

int main(int argc, char **argv)
{
	int argindex = 0;
	char *file;
	struct string_list *filelist = NULL;
	struct knode *kn;

	DBG(setbuf(stdout, NULL);)

	if (argc <= 1) {
		puts(helptext);
		exit(0);
	}

	argindex = get_options(&argv[1]);
	argv += argindex;
	argc -= argindex;

	if (!kabi_sql_filename)
		kabi_sql_filename = "../kabitree.sql";

	DBG(puts("got the files");)
	symlist = sparse_initialize(argc, argv, &filelist);

	DBG(puts("created the symlist");)
	kn = alloc_knode();
	add_knode(&knodes, kn);
	kn->parent = kn;

	FOR_EACH_PTR_NOTAG(filelist, file) {
		DBG(printf("sparse file: %s\n", file);)
		symlist = sparse(file);
		kn = map_knode(kn, NULL, CTL_FILE);
		kn->name = file;
		kn->level = 0;
		build_tree(symlist, kn);
	} END_FOR_EACH_PTR_NOTAG(file);

	DBG(printf("\nhiwater: %d\n", hiwater);)

	if (!sql_open(kabi_sql_filename, kabi_table_name))
		sql_init(kabi_sql_filename, kabi_table_schema, kabi_table_name);

	sql_prepare_kabi_stmts();
	show_knodes(knodes);
	sql_finalize_kabi_stmts();
	sql_close(db);

	return 0;
}
