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
 * 1. Find all the exported symbols. They will be listed with a prefix of
 *    "EXPORTED:". Example ...
 *    EXPORTED: function foo
 * 2. Find all the non-scalar (scalar as an option) args of the exported
 *    symbols. These will be listed with a prefix of "ARG:". Example...
 *    ARG: union fee
 * 3. List all the members of nonscalar types used by the nonscalar args of
 *    exported symbols. They will be listed with the prefix "NESTED: "
 *    Additionally, the symbols in which these nonscalar members were
 *    declared will also be listed with the prefix "IN: " followed by the
 *    name (ident->name, see sparse/token.h) of the symbols in which they
 *    were declared. Example...
 *    NESTED: struct bar
 *    IN: struct fee
 *    IN: union fii
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
//#define NDEBUG	// comment-out to enable runtime asserts
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
static int kp_verbose = 0;
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
	CTL_ARG		= 1 << 5,
	CTL_NESTED	= 1 << 6,
	CTL_NOOUT	= 1 << 7,
	CTL_DEBUG	= 1 << 16
};

static inline int strequ(const char *s1, const char *s2) {
	return strcmp(s1, s2) == 0;
}

// Hierarchical Symbols and List
//
struct hiersym {
	struct symbol *symbol;
	struct symbol *parent;
	enum ctlflags	flags;
	int level;		// hierarchical level
};

DECLARE_PTR_LIST(hiersym_list, struct hiersym);

static inline void add_hiersym(struct symbol_list **list, struct hiersym *hsym)
{
	add_ptr_list(list, hsym);
}

static const char *get_modstr(unsigned long mod)
{
	static char buffer[100];
	int len = 0;
	int i;
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
	if (mod == MOD_SIGNED)
		return "int";
	if (mod == MOD_UNSIGNED)
		return "unsigned int";
	if (STD_SIGNED(mod, MOD_CHAR))
		return "char";
	if (STD_SIGNED(mod, MOD_LONG))
		return "long";
	if (STD_SIGNED(mod, MOD_LONGLONG))
		return "long long";
	if (STD_SIGNED(mod, MOD_LONGLONGLONG))
		return "long long long";

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
			while ((c = *name++) != '\0' && len + 2 < sizeof buffer)
				buffer[len++] = c;
			buffer[len++] = ' ';
		}
	}
	buffer[len] = 0;
	return buffer;
}

#if 0
static void show_syms(
	struct symbol_list *list,
	struct symbol_list **optlist,
	enum typemask id);

static void dump_list(
	struct symbol_list *list,
	struct symbol_list **optlist,
	enum typemask id);
#endif

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
static struct symbol *find_internal_exported(
		struct symbol_list *symlist,
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

// lookup_sym - see if the symbol is already in the list
//
// This routine is called before adding a symbol to a list to assure that
// the symbol is not already in the list.
//
// list - pointer to the head of the symbol list
// sym - pointer to the symbol that is being sought
//
// returns 1 if symbol is there, 0 if symbol is not there.
//
static int lookup_sym(struct symbol_list *list, struct symbol *sym)
{
	struct symbol *temp;

	FOR_EACH_PTR(list, temp) {
		if (temp == sym)
			return 1;
	} END_FOR_EACH_PTR(temp);

	return 0;
}

// add_uniquesym - add a symbol of struct type to the structargs symbol list
//
// sym - pointer to the symbol to add, if it's not already in the list.
//
// list - address of a pointer to a symbol_list to which the symbol will be
//        added if it's not already there.
//
// libsparse calls:
//        add_symbol
//
static void add_uniquesym(struct symbol *sym, struct symbol_list **list)
{
	if (! lookup_sym(*list, sym))
		add_symbol(list, sym);
}

// explore_ctype(struct symbol *sym, symbol_list *list)
//
// Recursively traverse the ctype tree to get the details about the symbol,
// i.e. type, name, etc.
//
// sym - pointer to symbol whose ctype.base_type tree we will recursively
//       explore
//
// list - address of the pointer to an optional symbol_list to which the
//        current sym will be added, if this parameter is not null and if
//        it matches the id argument.
//
// id - if list is not null, this argument identifies the type of symbol
//      to be added to the list (see sparse/symbol.h).
//
// flags - flags to control execution depending on the caller.
//
// libsparse calls:
//      symbol.c::get_type_name()
//
static void explore_ctype(
	struct symbol *sym,
	struct symbol_list **list,
	enum typemask id,
	enum ctlflags *flags)
{
	struct symbol *basetype = sym->ctype.base_type;
	enum typemask tm;

	if (basetype) {

		if (basetype->type) {
			tm = 1 << basetype->type;
			const char *typnam = get_type_name(basetype->type);

			if (basetype->type == SYM_BASETYPE) {
				if (! basetype->ctype.modifiers)
					typnam = "void";
				else
					typnam = get_modstr
						(basetype->ctype.modifiers);
			}

			if (basetype->type ==SYM_PTR)
				*flags |= CTL_POINTER;
			else
				prverb("%s ", typnam);

			if (basetype->ctype.modifiers
			 & (MOD_LONGLONG | MOD_LONGLONGLONG))
				prverb("(%d-bit) ", basetype->bit_size);

			if (list && (tm & id))
				add_uniquesym(basetype, list);
		}

		if (basetype->ident)
			prverb("%s ", basetype->ident->name);

		explore_ctype(basetype, list, id, flags);
	}
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
	unsigned long flg = flags & (CTL_EXPORTED | CTL_ARG | CTL_NESTED);

	assert(count_bits(flg) <= 1);

	switch (flg) {
	case CTL_ARG:
		return "ARG: ";
	case CTL_NESTED:
		return "NESTED: ";
	}
	return NULL;
}

// show_syms(struct symbol_list *list, symbol_list **optlist, enum typemask id)
//
// Dump the list of symbols in the symbol_list
//
// list - a symbol_list of struct symbol
//
// optlist - address of the pointer to an optional symbol_list. If a match is
//           made between the symbol's type and the optional id argument
//           (see below), then the symbol will be added to that optionial list.
//
// id - the type of the symbol, see enum typemask above and enum type in
//      sparse/symbol.h
//
static void show_syms(
	struct symbol_list *list,
	struct symbol_list **optlist,
	enum typemask id)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		enum ctlflags flags = 0;
		char *fmt = "%s\n";

		prverb("%s", spacer);
		explore_ctype(sym, optlist, id, &flags);

		if (sym->ident) {
			if (flags & CTL_POINTER)
				fmt = "*%s\n";
			prverb(fmt, sym->ident->name);
		}

	} END_FOR_EACH_PTR(sym);
}

// show_args(struct symbol *sym)
//
// Determines if the symbol arg is a function. If so, prints the argument
// list.
//
// Returns void
//
static void show_args (struct symbol *sym)
{
	if (sym->arguments)
		show_syms(sym->arguments, &structargs, (SM_STRUCT | SM_UNION));

	prverb("\n");
}

static int starts_with(const char *a, const char *b)
{
	if(strncmp(a, b, strlen(b)) == 0)
		return 1;

	return 0;
}

// show_exported - show the internal declaration of an exported symbol
//
// sym - pointer to the symbol list
//
// implicit inputs:
//
//      symlist - globally declared symbol list, initialized by a call to
//                sparse() in main().
//
//      exported - globally declared symbol list that will contain the
//                 pointers to the internally declared struct symbols of
//                 the exported struct symbols.
//
//      structargs - globally declared symbol list that will contain the
//                   pointers to symbols that were in the argument lists
//                   of exported functions.
//
// libsparse calls:
//      add_symbol
//
static void show_exported(struct symbol *sym)
{
	struct symbol *exp;
	enum ctlflags flags = CTL_EXPORTED;

	if (starts_with(sym->ident->name, ksymprefix)) {
		int offset = strlen(ksymprefix);
		char *symname = &sym->ident->name[offset];

		// Find the internal declaration of the exported symbol and
		// add it to the "exported" list.
		//
		if ((exp = find_internal_exported(symlist, symname))) {
			struct symbol *basetype = exp->ctype.base_type;

			add_symbol(&exported, exp);
			printf("EXPORTED: ");
			explore_ctype(exp,
					&structargs,
					(SM_STRUCT | SM_UNION),
					&flags);

			if (kp_verbose && (flags & CTL_POINTER))
				putchar('*');

			printf("%s\n", symname);

			// If the exported symbol is a function, print its
			// args.
			//
			if (basetype->type == SYM_FN) {
				show_args(basetype);
			}
		} else
			printf("Could not find internal source.\n");
	}
}

static void process_file()
{
	struct symbol *sym;

	FOR_EACH_PTR(symlist, sym) {
		show_exported(sym);
	} END_FOR_EACH_PTR(sym);
}

static void dump_list(
	struct symbol_list *list,
	struct symbol_list **optlist,
	enum typemask id,
	enum ctlflags flags)
{
	struct symbol *sym;
	char *prefix = get_prefix(flags);

	FOR_EACH_PTR(list, sym) {

		if (kp_verbose)
			putchar ('\n');

		if (prefix)
			printf("%s%s ", prefix, get_type_name(sym->type));
		else
			printf("%s ", get_type_name(sym->type));

		if (sym->ident->name)
			printf("%s\n", sym->ident->name);

		if (sym->symbol_list)
			show_syms(sym->symbol_list, optlist, id);

	} END_FOR_EACH_PTR(sym);
}

static int parse_opt(char opt, int state)
{
	int validopt = 1;

	switch (opt) {
	case 'v' : kp_verbose = state;	// kp_verbose is global to this file
		   break;
	case 'h' : puts(helptext);
		   exit(0);
	default  : validopt = 0;
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

int main(int argc, char **argv)
{
	int argindex = 0;
	char *file;
	struct string_list *filelist = NULL;

	if (argc <= 1) {
		puts(helptext);
		exit(0);
	}

	argindex = get_options(argv);
	argv += argindex;
	argc -= argindex;

	symlist = sparse_initialize(argc, argv, &filelist);

	FOR_EACH_PTR_NOTAG(filelist, file) {
		printf("FILE: %s\n", file);
		symlist = sparse(file);
		process_file();
	} END_FOR_EACH_PTR_NOTAG(file);

	dump_list(structargs, &nested, (SM_STRUCT | SM_UNION), CTL_ARG);
	dump_list(nested, NULL, 0, CTL_NESTED);

	if (kp_verbose)
		putchar('\n');

	return 0;
}

