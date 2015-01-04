/* kabi.c
 *
 * Find all the exported symbols in .i file(s) passed as argument(s) on the
 * command line when invoking this executable.
 *
 * Relies heavily on the sparse project. See https://www.openhub.net/p/sparse
 * Include files for sparse in Fedora are located in /usr/include/sparse when
 * sparse is built and installed.
 *
 * Method for identifying exported symbols was inspired by the spartakus
 * project: http://git.engineering.redhat.com/git/users/sbairagy/spartakus.git
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sparse/lib.h>
#include <sparse/allocate.h>
#include <sparse/parse.h>
#include <sparse/symbol.h>
#include <sparse/expression.h>
#include <sparse/token.h>

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
static struct symbol_list *exported = NULL;
static struct symbol_list *symlist = NULL;
static struct symbol_list *structargs = NULL;
static struct symbol_list *level1_structs = NULL;
#if 0
static struct symbol_list *level2_structs = NULL;
static struct symbol_list **structlist[] = {
	&structargs,
	&level1_structs,
	&level2_structs,
};
#endif

// Verbose printf control
//
#define prverb(fmt, ...) \
do { \
	if (kp_verbose) \
		printf(fmt, ##__VA_ARGS__); \
} while (0)

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

static void show_syms(
	struct symbol_list *list,
	struct symbol_list **optlist,
	enum typemask id);

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
// libsparse calls:
//      symbol.c::get_type_name()
//      show-parse.c::modifier_string()
//
static void explore_ctype(
	struct symbol *sym,
	struct symbol_list **list,
	enum typemask id)
{
	struct symbol *basetype = sym->ctype.base_type;
	enum typemask tm;

	if (basetype) {

		if (basetype->type) {
			tm = 1 << basetype->type;
			const char *typnam = get_type_name(basetype->type);

			if (strcmp(typnam, "basetype") == 0) {

				if (! basetype->ctype.modifiers)
					typnam = "void";
				else
					typnam = modifier_string
						(basetype->ctype.modifiers);
			}

			if (list && (tm & id))
				add_uniquesym(basetype, list);

			prverb("%s ", typnam);
		}

		if (basetype->ident)
			prverb("%s ", basetype->ident->name);

		explore_ctype(basetype, list, id);
	}
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
		prverb("\t\t");
		explore_ctype(sym, optlist, id);
		if (sym->ident)
			prverb("%s\n", sym->ident->name);
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
	if (sym->arg_count)
		prverb("\targ_count: %d ", sym->arg_count);

	if (sym->arguments) {
		prverb("\n\t\targuments:\n");
		show_syms(sym->arguments, &structargs, (SM_STRUCT | SM_UNION));
	}
	else if (kp_verbose)
		putchar('\n');
}

static int starts_with(const char *a, const char *b)
{
	if(strncmp(a, b, strlen(b)) == 0)
	{
		return 1;
	}
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
	char pre = kp_verbose ? '\n' : '\0';
	char suf = kp_verbose ? ' '  : '\n';

	// Symbol name beginning with __ksymtab_ is an exported symbol
	//
	if (starts_with(sym->ident->name, ksymprefix)) {
		int offset = strlen(ksymprefix);
		char *symname = &sym->ident->name[offset];

		printf("%c%s%c", pre, symname, suf);

		// Find the internal declaration of the exported symbol and
		// add it to the "exported" list.
		//
		if ((exp = find_internal_exported(symlist, symname))) {
			struct symbol *basetype = exp->ctype.base_type;

			add_symbol(&exported, exp);
			explore_ctype(exp, &structargs, (SM_STRUCT | SM_UNION));

			// If the exported symbol is a function, print its
			// args.
			//
			if (basetype->type == SYM_FN)
				show_args(basetype);
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
	enum typemask id)
{
	struct symbol *sym;
	char nl = kp_verbose ? '\n' : '\0';

	FOR_EACH_PTR(list, sym) {
		printf("%c%s ", nl, get_type_name(sym->type));

		if (sym->ident->name)
			printf("%s\n", sym->ident->name);

		if (sym->symbol_list)
			show_syms(sym->symbol_list, optlist, id);

	} END_FOR_EACH_PTR(sym);
}

static void repeat_char(char c, int r)
{
	while (r--)
		putchar(c);
}

static void print_banner(char *banner)
{
	int banlen = strlen(banner);
	putchar('\n');
	repeat_char('=', banlen);
	putchar('\n');
	puts(banner);
	repeat_char('=', banlen);
	putchar('\n');
}

static int parse_opt(char opt, int on)
{
	int validopt = 1;

	switch (opt) {
	case 'v' : kp_verbose = on;
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
	int on = 0;
	int index = 0;

	if (**args == '-') {
		on = (*args)[strlen(*args)-1] == '-' ? 0 : 1;
		index += parse_opt((*args)[1], on);
		++args;
	}

	return index;
}

int main(int argc, char **argv)
{
	int argindex = 0;
	char *file;
	struct string_list *filelist = NULL;

	argindex = get_options(argv);
	argv += argindex;
	argc -= argindex;

	symlist = sparse_initialize(argc, argv, &filelist);

	print_banner(" ** Exported Symbols ** ");

	FOR_EACH_PTR_NOTAG(filelist, file) {
		printf("\nfile: %s\n", file);
		symlist = sparse(file);
		process_file();
	} END_FOR_EACH_PTR_NOTAG(file);

	print_banner(" Structs passed as arguments to exported functions ");
	dump_list(structargs, &level1_structs, (SM_STRUCT | SM_UNION));

	print_banner(" Structs declared within structs passed as arguments ");
	dump_list(level1_structs, NULL, 0);
	putchar('\n');

	return 0;
}

