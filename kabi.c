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
 * 1. Find all the exported symbols.
 * 2. Find all the non-scalar (scalar as an option) args of the exported
 *    symbols.
 * 3. List all the members of nonscalar types used by the nonscalar args of
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

#define STD_SIGNED(mask, bit) (mask == (MOD_SIGNED | bit))

#define prverb(fmt, ...) \
do { \
	if (kp_verbose) \
		printf(fmt, ##__VA_ARGS__); \
} while (0)

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
typedef unsigned int bool;

const char *spacer = "  ";

const char *helptext =
"\n"
"kabi [options] filespec\n"
"\n"
"Parses \".i\" (intermediate, c-preprocessed) files for exported symbols and\n"
"symbols of structs and unions that are used by the exported symbols.\n"
"\n"
"Options:\n"
"	-v	verbose, lists all the arguments of functions and members\n"
"		of structs and unions.\n"
"\n"
"	-v-	concise (default), lists only the exported symbols and\n"
"		symbols for structs and unions that are used by the exported\n"
"		symbols.\n"
"\n"
"	-h	This help message.\n"
"\n"
"filespec:\n"
"		file or files (wildcards ok) to be processed\n"
"\n";

static const char *ksymprefix = "__ksymtab_";
static bool kp_verbose = false;
static bool filelist = false;
static int hiwater = 0;
static struct symbol_list *exported	= NULL;
static struct symbol_list *symlist	= NULL;
static struct symbol_list *structargs 	= NULL;
static struct symbol_list *nested	= NULL;

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
	CTL_UNIQUE	= 1 << 9,
	CTL_FILE	= 1 << 10,
};

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

struct knode {
	struct knode *parent;
	struct knodelist *children;
	struct symbol *symbol;
	struct symbol_list *symbol_list;
	struct string_list *declist;
	char *name;
	char *typnam;		// only exists to prevent infinite recursion
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

static struct knode *map_knode(struct knode *parent, struct symbol *symbol)
{
	struct knode *newknode;
	newknode = alloc_knode();
	newknode->parent = parent;
	newknode->flags = 0;
	newknode->symbol = symbol;
	newknode->level = parent->level + 1;
	add_knode(&parent->children, newknode);
	if (newknode->level > hiwater)
		hiwater = newknode->level;
	return newknode;
}

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

static void dump_declist(struct knode *kn)
{
	char *str;
	FOR_EACH_PTR_NOTAG(kn->declist, str) {
		printf("%s ", str);
	} END_FOR_EACH_PTR_NOTAG(str);

	if (kn->flags & CTL_POINTER)
		putchar('*');

	if (kn->name)
		puts(kn->name);
	else
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

static struct symbol *find_internal_exported(struct symbol_list *, char *);
static bool starts_with(const char *a, const char *b);
static const char *get_modstr(unsigned long mod);
static void get_declist(struct knode *kn, struct symbol *sym);
static void get_symbols
		(struct knode *parent,
		 struct symbol_list *list,
		 enum ctlflags flags);

static struct symbol *find_symbol_match(struct symbol_list *list, struct symbol *sym)
{
	struct symbol *temp;
	FOR_EACH_PTR(list, temp) {
		if (sym == temp)
			return temp;
	} END_FOR_EACH_PTR(temp);
	return NULL;
}

static struct symbol *get_nth_symbol(struct symbol_list *list, int index)
{
	struct symbol *temp;
	int count = 0;
	if (index > symbol_list_size(list))
		return NULL;
	FOR_EACH_PTR(list, temp) {
		if (count++ == index)
			return temp;
	} END_FOR_EACH_PTR(temp);
	return NULL;
}

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
	const char *typnam;

	if (! basetype)
		goto out;

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
		&& (basetype->symbol_list))
			add_symbol(&kn->symbol_list, basetype);
	}

	if (basetype->ident) {
		kn->typnam = basetype->ident->name;
		add_string(&kn->declist, basetype->ident->name);
	}
	else
		kn->typnam = (char *)typnam;

	get_declist(kn, basetype);
out:
	return;
}

static void get_symbols
		(struct knode *parent,
		 struct symbol_list *list,
		 enum ctlflags flags)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct knode *kn;
		struct used *user;

		if (parent->symbol == sym)
			return;

		if (add_to_used(&redlist, parent, sym))
			return;

		kn = map_knode(parent, sym);
		kn->flags |= flags;
		get_declist(kn, sym);

		if (sym->ident)
			kn->name = sym->ident->name;

		// If these conditions are not met, recursion into the lower
		// list will be infinite. This most often occurs when a struct
		// has members that point back to itself, as in ..
		// struct foo {
		// 	foo *next;
		// 	foo *prev;
		// };
		if ((kn->symbol_list && ! parent->typnam)
		|| (parent->typnam && strcmp(parent->typnam, kn->typnam)))
			proc_symlist(kn, kn->symbol_list, CTL_NESTED);

	} END_FOR_EACH_PTR(sym);
}

static void build_knode(struct knode *parent, char *symname)
{
	struct symbol *sym;

	if ((sym = find_internal_exported(symlist, symname))) {
		struct symbol *basetype = sym->ctype.base_type;
		struct knode *kn = map_knode(parent, sym);

		kn->flags = CTL_EXPORTED;
		kn->name = symname;
		get_declist(kn, basetype);

		if (kn->symbol_list)
			proc_symlist(kn, kn->symbol_list, CTL_RETURN | CTL_GODEEP);

		if (basetype->arguments)
			get_symbols(kn, basetype->arguments, CTL_ARG);
	}
}

static void build_tree(struct symbol_list *symlist, struct knode *parent)
{
	struct symbol *sym;

	FOR_EACH_PTR(symlist, sym) {

		if (starts_with(sym->ident->name, ksymprefix)) {
			int offset = strlen(ksymprefix);
			char *symname = &sym->ident->name[offset];
			build_knode(parent, symname);
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

static bool starts_with(const char *a, const char *b)
{
	if(strncmp(a, b, strlen(b)) == 0)
		return true;

	return false;
}

static bool parse_opt(char opt, int state)
{
	bool validopt = true;

	switch (opt) {
	case 'v' : kp_verbose = state;	// kp_verbose is global to this file
		   break;
	case 'f' : filelist = state;
	case 'h' : puts(helptext);
		   exit(0);
	default  : validopt = false;
		   break;
	}

	return validopt;
}

static int get_options(char **argv)
{
	char **args = &argv[1];
	int state = 0;		// on = 1, off = 0
	int index = 0;

	if (**args == '-') {

		// Trailing '-' sets state to OFF (0)
		//
		state = (*args)[strlen(*args)-1] != '-';
		index += parse_opt((*args)[1], state);
		++args;
	}

	return index;
}

static void format_declist(struct knode *kn, bool usespace)
{
	char *decl;
	int declcount = string_list_size(kn->declist);
	int counter = 1;
	char *fmt = usespace ? " %s " : "%s ";

	FOR_EACH_PTR_NOTAG(kn->declist, decl) {
		if (counter++ == declcount
		&& kn->flags & CTL_POINTER
		&& ! kn->name)
			putchar('*');
		printf(fmt, decl);
	} END_FOR_EACH_PTR_NOTAG(decl);

	if (kn->name) {
		if (kn->flags & CTL_POINTER)
			putchar('*');
		printf("%s\n", kn->name);
	}
	else if (counter > 1)
		putchar('\n');
}

static void show_flagged_knodes(struct knodelist *klist, enum ctlflags flags)
{
	struct knode *kn;

	FOR_EACH_PTR(klist, kn) {

		struct knode *child;

		if (kn->flags & CTL_FILE) {
			printf("%s %s\n", get_prefix(kn->flags), kn->name);
			show_flagged_knodes(kn->children, flags);
			continue;
		}

		if  (! ((kn->flags & flags) | (flags & CTL_RETURN)))
			continue;

		printf("%s", get_prefix(kn->flags));
		format_declist(kn, false);

		if (kn->flags & CTL_RETURN) {
			printf("%s ", get_prefix(kn->flags));
			format_declist(kn, false);
		}

		FOR_EACH_PTR(kn->children, child) {
			format_declist(child, true);
		} END_FOR_EACH_PTR(child);
	} END_FOR_EACH_PTR(kn);
}

static void show_knodes(struct knodelist *klist)
{
	struct knode *kn;

	FOR_EACH_PTR(klist, kn) {
		char *decl;
		int declcount = string_list_size(kn->declist);
		int counter = 1;

		printf("%s ", pad_out(kn->level, ' '));

		printf("%s%-2d ", get_prefix(kn->flags), kn->level);

		FOR_EACH_PTR_NOTAG(kn->declist, decl) {
			if (counter++ == declcount
			&& kn->flags & CTL_POINTER
			&& ! kn->name)
				putchar('*');
			printf("%s ", decl);
		} END_FOR_EACH_PTR_NOTAG(decl);

		if (kn->name) {
			if (kn->flags & CTL_POINTER)
				putchar('*');
			printf("%s\n", kn->name);
		}
		else if (counter > 1)
			putchar('\n');

		if (kn->children)
			show_knodes(kn->children);

	} END_FOR_EACH_PTR(kn);
}

int main(int argc, char **argv)
{
	int argindex = 0;
	char *file;
	struct string_list *filelist = NULL;
	struct knode *kn;

	//setbuf(stdout, NULL);

	if (argc <= 1) {
		puts(helptext);
		exit(0);
	}

	argindex = get_options(argv);
	argv += argindex;
	argc -= argindex;

	symlist = sparse_initialize(argc, argv, &filelist);

	kn = alloc_knode();
	add_knode(&knodes, kn);
	kn->parent = kn;

	FOR_EACH_PTR_NOTAG(filelist, file) {
		symlist = sparse(file);
		kn->name = file;
		kn->flags = CTL_FILE;
		build_tree(symlist, kn);
	} END_FOR_EACH_PTR_NOTAG(file);

	//show_flagged_knodes(knodes, (CTL_EXPORTED | CTL_RETURN));
	//show_flagged_knodes(knodes, CTL_ARG);

	printf("\nhiwater: %d\n", hiwater);
	show_knodes(knodes);

	if (kp_verbose)
		putchar('\n');

	return 0;
}

