/* kabi.c
 *
 * Find all the exported symbols in .i file(s) passed as argument(s) on the
 * command line when invoking this executable.
 *
 * Relies heavily on the sparse project. See https://www.openhub.net/p/sparse
 *
 * Method for identifying exported symbols was inspired by the spartakus
 * project: http://git.engineering.redhat.com/git/users/sbairagy/spartakus.git
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sparse/lib.h"
#include "sparse/allocate.h"
#include "sparse/parse.h"
#include "sparse/symbol.h"
#include "sparse/expression.h"
#include "sparse/token.h"

const char *ksymprefix = "__ksymtab_";
static struct symbol_list *exported = NULL;
static struct symbol_list *symlist = NULL;
static struct symbol_list *structargs = NULL;

// find_internal_exported (symbol_list* symlist, char *symname)
//
// Finds the internal declaration of an exported symbol in the symlist.
// The symname parameter must have the "__ksymtab_" prefix stripped from
// the exported symbol name.
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
// sym - pointer to the symbol to lookup
//
// implicit inputs:
//
//      structargs - globally declared symbol_list of structs passed as
// 	             arguments.
//
static void add_struct( struct symbol *sym)
{
	if (! lookup_sym(structargs, sym))
		add_symbol(&structargs, sym);
}

// explore_ctype(struct symbol *sym)
//
// Recursively traverse the ctype tree to get the details about the symbol,
// i.e. type, name, etc.
//
// sparse calls:
// 	symbol.c::get_type_name()
// 	show-parse.c::modifier_string()
//
static void explore_ctype(struct symbol *sym)
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

			if (basetype->type == SYM_STRUCT)
				add_struct(basetype);

			printf("%s ", typnam);
		}
		if (basetype->ident)
			printf("%s ", basetype->ident->name);

		explore_ctype(basetype);
	}
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
		struct symbol *arg = NULL;

		printf("\n\t\targuments:\n");

		FOR_EACH_PTR(sym->arguments, arg) {
			printf("\t\t");
			explore_ctype(arg);
			printf("%s\n", arg->ident->name);
		} END_FOR_EACH_PTR(arg);
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
//      exported - globally declared symbol list that will contain the
//                 pointers to the internally declared struct symbols of
//                 the exported struct symbols.
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
			explore_ctype(exp);

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

	printf("\nIdentify Exported Symbols and "
		"their Arguments and Members.\n");

	symlist = sparse_initialize(argc, argv, &filelist);
	show_symbol_list(symlist, "\n");

	FOR_EACH_PTR_NOTAG(filelist, file)  {
		printf("\nfile: %s\n", file);
		symlist = sparse(file);
		process_file();
	} END_FOR_EACH_PTR_NOTAG(file);

	printf("\nstructs passed as arguments to exported functions\n");

	FOR_EACH_PTR(structargs, sym) {
		if (sym->ident->name) {
			printf ("%s\n", sym->ident->name);
		}
	} END_FOR_EACH_PTR(sym);

	return 0;
}

