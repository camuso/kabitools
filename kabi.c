/* kabi.c
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
 * Gather all the exported symbols and recursively descend into the
 * compound types in the arg lists to acquire all information on compound
 * types that can affect the exported symbols.
 *
 * Symbols will be recursively explored only once. To recursively explore
 * each symbol wherever it occurs would lead to an unwieldy database.
 *
 * Compound symbols and their complete descendancy are kept in a separate
 * table.
 *
 * Two tables are generated. One containing all the kabi data for each file
 * parsed, and the other containing one complete definition of each compound
 * structure and all its descendants. These two tables are stored in CSV
 * (comma separated values) files and can be imported to a database.
 *
 * The csv files are opened to be appended, not to be created from scratch,
 * unless they don't yet exist. In this way, directories and filenames with
 * wildcards, or the bash utility "find", can be used to process multiple
 * files, appending the results from each file processed to the CSV files.
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
#include <stdint.h>
#include <time.h>
#define NDEBUG	// comment out to enable asserts
#include <assert.h>

#include <sparse/symbol.h>

#include "checksum.h"
#include "kabi.h"
#include "kabi-serial.h"

#define STD_SIGNED(mask, bit) (mask == (MOD_SIGNED | bit))
#define STRBUFSIZ 256



#if !defined(NDEBUG)
#define DBG(x) x
#define RUN(x)
#else
#define DBG(x)
#define RUN(x) x
#endif

/*****************************************************
** Global declarations
******************************************************/

static const char *helptext ="\
\n\
kabi [options] files \n\
\n\
    Parses \".i\" (intermediate, c-preprocessed) files for exported \n\
    symbols and symbols of structs and unions that are used by the \n\
    exported symbols. \n\
\n\
    files   - File or files (wildcards ok) to be processed\n\
              Must be last in the argument list. \n\
\n\
Options:\n\
    -d file   optional filename for csv file containing the kabi tree. \n\
              The default is \"../kabi-data.csv\". \n\
    -t file   Optional filename for csv file containing all the kabi types. \n\
              The default is \"../kabi-types.csv\". \n\
    -v        Verbose (default): Lists all the arguments of functions and\n\
              recursively descends into compound types to gather all the\n\
              information about them.\n\
    -x        Delete the data files before starting. \n\
    -h        This help message.\n\
\n";

static const char *ksymprefix = "__ksymtab_";
static bool kp_verbose = true;
static bool kp_rmfiles = false;
static bool showusers = false;
DBG(static int hiwater = 0;)
static struct symbol_list *symlist = NULL;
static bool kabiflag = false;

static char *datafilename = "../kabi-data.csv";
static char *typefilename = "../kabi-types.csv";
FILE *datafile;
FILE *typefile;


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
** knode declaration and utilities
******************************************************/

struct knode {
	struct knode *primordial;
	struct knode *parent;
	struct knodelist *parents;
	struct knodelist *children;
	struct symbol *symbol;
	struct symbol_list *symbol_list;
	struct string_list *declist;
	struct usedlist *redlist;
	char *name;
	char *typnam;
	char *file;
	enum ctlflags flags;
	int level;
	long left;
	long right;
	unsigned long crc;
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

static inline struct knode *first_knode(struct knodelist *head)
{
	return first_ptr_list((struct ptr_list *)head);
}

static inline struct knode *last_knode(struct knodelist *head)
{
	return last_ptr_list((struct ptr_list *)head);
}

static inline void *delete_last_knode(struct knodelist  **list)
{
	return (struct knode *)delete_ptr_list_last((struct ptr_list **)list);
}

static inline int knode_list_size(struct knodelist *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

long get_timestamp()
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (ts.tv_sec << 32) + ts.tv_nsec;
}

// map_knode - allocate and initialize a new knode
//
// parent - pointer to the parent knode
// symbol - the symbol characterized by this knode
// flags  - for processing and type information
//
// returns a pointer to the new instance of knode.
//
// Every call to map_knode should have a corresponding update of
// the 'right' field with a call to get_timestamp() when processing
// for the knode is complete. This implements a nested hierarchy
// using the "modified preorder tree traversal algorithm".
struct knode *map_knode(struct knode *parent,
			       struct symbol *symbol,
			       enum ctlflags flags)
{
	struct knode *kn;
	long timestamp = get_timestamp();

	kn = alloc_knode();
	kn->primordial = parent->primordial;
	kn->parent = parent;
	kn->file = parent->file;
	kn->flags |= flags;
	kn->symbol = symbol;
	kn->level = parent->level + 1;
	kn->redlist = parent->redlist;
	kn->left = timestamp;

	add_knode(&parent->children, kn);
	DBG(hiwater= kn->level > hiwater ? kn->level : hiwater;)
	return kn;
}

/*****************************************************
** used declaration and utilities
******************************************************/

struct used {
	struct symbol *symbol;
	struct knodelist *users;
	char typnam[STRBUFSIZ];
};

DECLARE_PTR_LIST(usedlist, struct used);

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

static inline struct knode *first_user(struct knodelist *list)
{
	return (struct knode *)first_ptr_list((struct ptr_list *)list);
}

static struct used *lookup_used(struct usedlist *list, struct knode *kn)
{
	struct used *temp;
	FOR_EACH_PTR(list, temp) {
		char sbuf[STRBUFSIZ];
		extract_type(kn, sbuf);

		if(!strncmp(temp->typnam, sbuf, STRBUFSIZ))
			return temp;

	} END_FOR_EACH_PTR(temp);
	return NULL;
}

static void add_new_used(struct usedlist **list,
			 struct knode *kn,
			 struct symbol *sym)
{
	char sbuf[STRBUFSIZ];
	struct used *newused = alloc_used();
	newused->symbol = sym;
	add_knode(&newused->users, kn->parent);
	add_used(list, newused);
	extract_type(kn, sbuf);
	strncpy(newused->typnam, sbuf, STRBUFSIZ);
}

bool is_used(struct usedlist **list,
		    struct knode *kn,
		    struct symbol *sym)
{
	struct used *user;

	if ((user = lookup_used(*list, kn))) {
		add_knode(&user->users, kn->parent);
		return true;
	} else {
		if (!(kn->flags & CTL_STRUCT))
			return false;
		add_new_used(list, kn, sym);
		return false;
	}
}

/*****************************************************
** Output formatting
******************************************************/

static void extract_type(struct knode *kn, char *sbuf)
{
	char *str;

	memset(sbuf, 0, STRBUFSIZ);

	FOR_EACH_PTR_NOTAG(kn->declist, str) {
		strcat(sbuf, str);
		strcat(sbuf, " ");
	} END_FOR_EACH_PTR_NOTAG(str);
}

static void compose_declaration(struct knode *kn, char *sbuf)
{
	extract_type(kn, sbuf);

	if (kn->flags & CTL_POINTER)
		strcat(sbuf, "*");

	if (kn->name)
		strcat(sbuf, kn->name);
}

static void format_declaration(struct knode *kn)
{
	char *sbuf = calloc(STRBUFSIZ, 1);
	struct knode *parent = kn->parent;

	compose_declaration(kn, sbuf);
	fprintf(datafile, ",%s ", sbuf);

	if (kn == parent)
		goto out;

	if (!parent || (kn->flags & CTL_FILE)) {
		fprintf(datafile, ",");
		goto out;
	}

	if (parent->declist) {
		compose_declaration(parent, sbuf);
		fprintf(datafile, ",%s ", sbuf);
	}
	else
		fprintf(datafile, ",%s ", parent->name);
out:
	free(sbuf);
	putc('\n', datafile);
}

static char *pad_out(int padsize, char padchar)
{
	static char buf[STRBUFSIZ];
	memset(buf, 0, STRBUFSIZ);
	while(padsize--)
		buf[padsize] = padchar;
	return buf;
}

/*****************************************************
** sparse probing and mining
******************************************************/

static struct symbol *find_internal_exported(struct symbol_list *, char *);
static const char *get_modstr(unsigned long mod);
static void get_declist(struct qnode *qn, struct knode *kn, struct symbol *sym);
static void get_symbols(struct qnode *,
			struct knode *parent,
			struct symbol_list *list,
			enum ctlflags flags);

static void proc_symlist(struct qnode *qparent,
			 struct knode *parent,
			 struct symbol_list *list,
			 enum ctlflags flags)
{
	struct symbol *sym;
	FOR_EACH_PTR(list, sym) {
		get_symbols(qparent, parent, sym->symbol_list, flags);
	} END_FOR_EACH_PTR(sym);
}

static void get_declist(struct qnode *qn, struct knode *kn, struct symbol *sym)
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

		if (basetype->type == SYM_PTR) {
			kn->flags |= CTL_POINTER;
			qn->flags |= CTL_POINTER;
		} else {
			add_string(&kn->declist, (char *)typnam);
			qn_add_to_declist(qn, (char *)typnam);
		}

		if ((tm & (SM_STRUCT | SM_UNION))
		&& (basetype->symbol_list)) {
			add_symbol(&kn->symbol_list, basetype);
			kn->flags |= CTL_STRUCT;
			qn->flags |= CTL_STRUCT;
		}

		if (tm & SM_FN) {
			kn->flags |= CTL_FUNCTION;
			qn->flags |= CTL_FUNCTION;
		}
	}

	if (basetype->ident) {
		kn->typnam = basetype->ident->name;
		qn->typnam = basetype->ident->name;
		add_string(&kn->declist, basetype->ident->name);
		qn_add_to_declist(qn, basetype->ident->name);
	} else {
		kn->typnam = (char *)typnam;
		qn->typnam = (char *)typnam;
	}

	get_declist(qn, kn, basetype);
}

static void get_symbols	(struct qnode *qparent,
			 struct knode *parent,
			 struct symbol_list *list,
			 enum ctlflags flags)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct knode *kn = NULL;
		struct qnode *qn = NULL;
		char sbuf[STRBUFSIZ];
		unsigned long crc;

		//if (parent->symbol == sym)
		//	return;

		kn = map_knode(parent, sym, flags);
		qn = new_qnode(qparent, flags);

		get_declist(qn, kn, sym);

		extract_type(kn, sbuf);
		kn->crc = raw_crc32(sbuf);

		qn_extract_type(qn, sbuf, STRBUFSIZ);
		crc = raw_crc32(sbuf);

		if (qn_is_dup(qn, qparent, crc))
			continue;

		qn->cn->crc = crc;

		if (sym->ident) {
			kn->name = sym->ident->name;
			qn->name = sym->ident->name;
		}

		printf("%s%s %s\n", pad_out(qn->cn->level, ' '), sbuf, qn->name);

		if (kn->symbol_list)
			proc_symlist(qn, kn, kn->symbol_list, CTL_NESTED);

#if 0
		// Avoid redundancies and infinite recursions.
		// Infinite recursions occur when a struct has members
		// that point back to itself, as in ..
		// struct list_head {
		// 	struct list_head *next;
		// 	struct list_head *prev;
		// };
		if (!(is_used(&kn->primordial->redlist, kn, sym)))
			proc_symlist(kn, kn->symbol_list, CTL_NESTED);
#endif
		kn->right = get_timestamp();
	} END_FOR_EACH_PTR(sym);
}

static void build_branch(struct knode *parent, char *symname, char *file)
{
	struct symbol *sym;

	if ((sym = find_internal_exported(symlist, symname))) {
		char sbuf[STRBUFSIZ];
		struct symbol *basetype = sym->ctype.base_type;
		struct knode *kn = map_knode(parent, NULL, CTL_EXPORTED);
		struct qnode *qn = new_qnode(NULL, CTL_EXPORTED);

		kn->file = file;
		kn->name = symname;
		kn->primordial = kn;

		qn->file = file;
		qn->name = symname;
		qn->cn->level = 1;

		kabiflag = true;
		get_declist(qn, kn, sym);

		extract_type(kn, sbuf);
		strcat(sbuf, kn->name);
		kn->crc = raw_crc32(sbuf);

		qn_extract_type(qn, sbuf, STRBUFSIZ);
		strcat(sbuf, qn->name);
		qn->cn->crc = raw_crc32(sbuf);

		printf("EXPORTED: %s\n", sbuf);

		if (kn->symbol_list) {
			struct knode *bkn = map_knode(kn, basetype, CTL_RETURN);
			struct qnode *bqn = new_qnode(qn, CTL_RETURN);

			get_declist(bqn, bkn, basetype);

			extract_type(bkn, sbuf);
			bkn->crc = raw_crc32(sbuf);
			bkn->right = get_timestamp();

			qn_extract_type(bqn, sbuf, STRBUFSIZ);
			bqn->cn->crc = raw_crc32(sbuf);

			printf("RETURN: %s\n", sbuf);
		}

		if (basetype->arguments)
			get_symbols(qn, kn, basetype->arguments, CTL_ARG);

		kn->right = get_timestamp();
	}
}

static inline bool begins_with(const char *a, const char *b)
{
	return (strncmp(a, b, strlen(b)) == 0);
}

static void build_tree(struct symbol_list *symlist,
		       struct knode *parent,
		       char *file)
{
	struct symbol *sym;

	FOR_EACH_PTR(symlist, sym) {

		if (sym->ident &&
		    begins_with(sym->ident->name, ksymprefix)) {
			int offset = strlen(ksymprefix);
			char *symname = &sym->ident->name[offset];
			build_branch(parent, symname, file);
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
		return "FILE";
	case CTL_EXPORTED:
		return "EXPORTED";
	case CTL_RETURN:
		return "RETURN";
	case CTL_ARG:
		return "ARG";
	case CTL_NESTED:
		return "NESTED";
	}
	return "";
}

static void show_users(struct knodelist *klist, int level)
{
	struct knode *kn;

	FOR_EACH_PTR(klist, kn) {

		if (kn->flags & CTL_STRUCT) {
			fprintf(datafile, "%s USER: ", pad_out(level+10, ' '));
			format_declaration(kn);
		}
	}END_FOR_EACH_PTR(kn);
}

static void write_knodes(struct knodelist *klist)
{
	struct knode *kn;

	FOR_EACH_PTR(klist, kn) {
		char *pfx = get_prefix(kn->flags);

		// Skip the top of the tree. It's just a placeholder
		if (kn->parent == kn)
			goto nextlevel;

		if (!kp_verbose && kn->flags & CTL_NESTED)
			return;

		fprintf(datafile, "%d,%08x,%lu,%lu,%s",
		       kn->level, (int)kn->crc, kn->left, kn->right, pfx);

		format_declaration(kn);

		if (showusers && (kn->flags & CTL_NESTED)) {
			struct used *u = lookup_used(kn->primordial->redlist, kn);
			if (u)
				show_users(u->users, kn->level);
		}
nextlevel:
		if (kn->children)
			write_knodes(kn->children);

	} END_FOR_EACH_PTR(kn);
}

static struct knode *get_kn_from_parent(char *typnam, struct knode *parent)
{
	struct knode *kn;
	FOR_EACH_PTR(parent->children, kn) {
		char sbuf[STRBUFSIZ];
		extract_type(kn, sbuf);
		if (!strncmp(sbuf, typnam, STRBUFSIZ))
			return kn;
	} END_FOR_EACH_PTR(kn);
	return NULL;
}

static void write_types(struct knodelist *klist)
{
	struct knode *kn;

	FOR_EACH_PTR(klist, kn) {

		struct usedlist *rlist;
		struct used *un;

		// Only interested in EXPORTED symbols. The redlists of
		// encountered types is kept there. The definition of each
		// type nested under an imported symbol is catalogued just
		// once for that symbol.
		if ((kn->parent == kn) || (!(kn->flags & CTL_EXPORTED)))
			goto nextlevel;

		rlist = kn->redlist;

		FOR_EACH_PTR(rlist, un) {
			char sbuf[STRBUFSIZ];
			struct knode *parent = first_user(un->users);
			struct knode *kn = get_kn_from_parent(un->typnam, parent);

			if (!kn)
				continue;

			fprintf(typefile, "%d,%lu,%lu,%08x",
				kn->level, kn->left, kn->right, kn->flags);
			extract_type(kn, sbuf);
			fprintf(typefile, ",%s \n", sbuf);
		} END_FOR_EACH_PTR(un);
nextlevel:
		if (kn->children)
			write_types(kn->children);

	} END_FOR_EACH_PTR(kn);
}

/*****************************************************
** Command line option parsing
******************************************************/

static bool parse_opt(char opt, char ***argv, int *index)
{
	int optstatus = 1;

	switch (opt) {
	case 'd' : datafilename = *((*argv)++);
		   ++(*index);
		   break;
	case 't' : typefilename = *((*argv)++);
		   ++(*index);
		   break;
	case 'v' : kp_verbose = true;
		   break;
	case 'u' : showusers = true;
		   break;
	case 'x' : kp_rmfiles = true;
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
	int index = 0;
	char *argstr;

	for (index = 0; *argv[0] == '-'; ++index) {
		int i;

		// Point to the first character of the actual option
		argstr = &(*argv++)[1];

		for (i = 0; argstr[i]; ++i)
			if (!parse_opt(argstr[i], &argv, &index))
				return false;
		if (!*argv)
			break;
	}

	return index;
}

static bool open_file(FILE **f, char *filename)
{
	if (!(*f = fopen(filename, "a+"))) {
		printf("Cannot open file \"%s\".\n", filename);
		return false;
	}
	return true;
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
		build_tree(symlist, kn, file);
		kn->right = get_timestamp();
	} END_FOR_EACH_PTR_NOTAG(file);

	DBG(printf("\nhiwater: %d\n", hiwater);)

	if (! kabiflag)
		return 1;

	if (kp_rmfiles) {
		remove(datafilename);
		remove(typefilename);
	}

	if (!open_file(&datafile, datafilename))
		return 1;

	write_knodes(knodes);

	if (!open_file(&typefile, typefilename))
		return 1;

	write_types(knodes);

	return 0;
}
