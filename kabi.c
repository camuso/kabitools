/* kabi.c
**
** The code that identifies exported symbols was lifted from the spartakus
** project: http://git.engineering.redhat.com/git/users/sbairagy/spartakus.git
**
**
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

typedef int bool;
#define true 1
#define false 0;

const char *ksymprefix = "__ksymtab_";
static struct symbol_list *exported = NULL;
static struct symbol_list *symlist = NULL;

// find_internal_exported (symbol_list* symlist, char *symname)
//
// Finds the internal declaration of an exported symbol in the symlist.
// The symname parameter must have the "__ksymtab_" prefix stripped from
// the exported symbol name.
//
// Returns a pointer to the symbol that corresponds to the exported one.
//
static struct symbol *find_internal_exported(struct symbol_list *symlist, char *symname)
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

static inline bool symbol_is_fp_type(struct symbol *sym)
{
	if (!sym)
		return false;

	return sym->ctype.base_type == &fp_type;
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
void explore_ctype(struct symbol *sym)
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

static void show_exported(struct symbol *sym)
{
	struct symbol *exp;

	// Symbol name beginning with __ksymtab_ is an exported symbol
	//
	if (starts_with(sym->ident->name, ksymprefix)) {
		int offset = strlen(ksymprefix);
		char *symname = &sym->ident->name[offset];

		printf("\n%s ", symname);

		// find the internal declaration of the exported symbol.
		//
		if (exp = find_internal_exported(symlist, symname)) {
			struct symbol *basetype = exp->ctype.base_type;

			add_symbol(&exported, exp);
			explore_ctype(exp);

			// If the exported symbol is a C function, print its
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

	printf("\nIdentify Exported Symbols and "
		"their Arguments and Members.\n");

	symlist = sparse_initialize(argc, argv, &filelist);
	show_symbol_list(symlist, "\n");

	FOR_EACH_PTR_NOTAG(filelist, file)  {
		printf("\nfile: %s\n", file);
		symlist = sparse(file);
		process_file();
	} END_FOR_EACH_PTR_NOTAG(file);

	return 0;
}

