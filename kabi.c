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

const char *ksymprefix = "__ksymtab_";
static struct symbol_list *exported = NULL;
static struct symbol_list *symlist = NULL;
static struct symbol_list *structargs = NULL;
static struct symbol_list *level1_structs = NULL;
static struct symbol_list *level2_structs = NULL;

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

// add_struct - add a symbol of struct type to the structargs symbol list
//
// sym - pointer to the symbol to add to the global structargs symbol_list.
//
// list - address of a pointer to a symbol_list to which the symbol will be
//        added.
//
// libsparse calls:
//        add_symbol
//
static void add_struct(struct symbol *sym, struct symbol_list **list)
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
// 	symbol.c::get_type_name()
// 	show-parse.c::modifier_string()
//
static void explore_ctype(
	struct symbol *sym,
	struct symbol_list **list,
	enum type id)
{
	struct symbol *basetype = sym->ctype.base_type;

	if (basetype) {
		if (basetype->type) {
			const char *typnam = get_type_name(basetype->type);

			if (strcmp(typnam, "basetype") == 0) {

				if (! basetype->ctype.modifiers)
					typnam = "void";
				else
					typnam = modifier_string
						(basetype->ctype.modifiers);
			}

			if (list && basetype->type == id)
				add_struct(basetype, list);

			printf("%s ", typnam);
		}
		if (basetype->ident)
			printf("%s ", basetype->ident->name);

		explore_ctype(basetype, list, id);
	}
}

// show_syms(struct symbol_list *list, symbol_list **optlist, enum type id)
//
// Dump the list of symbols in the symbol_list
//
// list - a symbol_list of struct symbol
//
// optlist - address of the pointer to an optional symbol_list. If a match is
//           made between the symbol's type and the optional id argument
//           (see below), then the symbol will be added to that optionial list.
//
// id - the type of the symbol, see enum type in sparse/symbol.h
//
static void show_syms(
	struct symbol_list *list,
	struct symbol_list **optlist,
	enum type id)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		printf("\t\t");
		explore_ctype(sym, optlist, id);
		printf("%s\n", sym->ident->name);
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
	struct symbol *basetype;

	if (sym->arg_count)
		printf("\targ_count: %d ", sym->arg_count);

	if (sym->arguments) {
		printf("\n\t\targuments:\n");
		show_syms(sym->arguments, &structargs, SYM_STRUCT);
	}
	else
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
// 	symlist - globally declared symbol list, initialized by a call to
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
// 	add_symbol
//
static void show_exported(struct symbol *sym)
{
	struct symbol *exp;

	// Symbol name beginning with __ksymtab_ is an exported symbol
	//
	if (starts_with(sym->ident->name, ksymprefix)) {
		int offset = strlen(ksymprefix);
		char *symname = &sym->ident->name[offset];

		printf("\n%s ", symname);

		// Find the internal declaration of the exported symbol and
		// add it to the "exported" list.
		//
		if (exp = find_internal_exported(symlist, symname)) {
			struct symbol *basetype = exp->ctype.base_type;

			add_symbol(&exported, exp);
			explore_ctype(exp, &structargs, SYM_STRUCT);

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

int main(int argc, char **argv)
{
	char *file;
	struct string_list *filelist = NULL;
	struct symbol *sym;

	symlist = sparse_initialize(argc, argv, &filelist);

	puts("\n************************************************"
	     "\nExported Symbols and their Arguments and Members"
	     "\n************************************************");

	FOR_EACH_PTR_NOTAG(filelist, file)  {
		printf("\nfile: %s\n", file);
		symlist = sparse(file);
		process_file();
	} END_FOR_EACH_PTR_NOTAG(file);

	puts("\n*************************************************"
	     "\nStructs passed as arguments to exported functions"
	     "\n*************************************************");

	FOR_EACH_PTR(structargs, sym) {
		if (sym->ident->name) {
			printf ("%s\n", sym->ident->name);
		}

		if (sym->symbol_list) {
			show_syms(sym->symbol_list, NULL, 0);
		}
	} END_FOR_EACH_PTR(sym);

	return 0;
}

