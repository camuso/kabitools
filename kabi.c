/* kabi.c
 *
 * This code was lifted directly from the spartakus project ...
 * http://git.engineering.redhat.com/git/users/sbairagy/spartakus.git
 * ... and modified for ksym detection.
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

int starts_with(const char *, const char *);
char * substring(const char* input, int offset, int len, char* dest);

static struct symbol_list *exported = NULL;
static struct symbol_list *symlist = NULL;

char *typenames[] = {
	[SYM_UNINITIALIZED] = "uninitialized",
	[SYM_PREPROCESSOR]  = "preprocessor",
	[SYM_BASETYPE]      = "basetype",
	[SYM_NODE]          = "node",
	[SYM_PTR]           = "pointer",
	[SYM_FN]            = "function",
	[SYM_ARRAY]         = "array",
	[SYM_STRUCT]        = "struct",
	[SYM_UNION]         = "union",
	[SYM_ENUM]          = "enum",
	[SYM_TYPEDEF]       = "typedef",
	[SYM_TYPEOF]        = "typeof",
	[SYM_MEMBER]        = "member",
	[SYM_BITFIELD]      = "bitfield",
	[SYM_LABEL]         = "label",
	[SYM_RESTRICT]      = "restrict",
	[SYM_FOULED]        = "fouled",
	[SYM_KEYWORD]       = "keyword",
	[SYM_BAD]           = "bad",
};

static void dump_symbol(struct symbol *sym)
{
	const char *typename;
	struct symbol *type;

	if (!sym) {
		puts("");
		return;
	}

	printf("TYPE: %s\n", show_typename(sym));
	type = sym->ctype.base_type;

	return;

	if (!type)
		return;

	switch (type->type) {
		struct symbol *member;

	case SYM_STRUCT:
	case SYM_UNION:
		if(member->ident) {
			puts(" {");
			FOR_EACH_PTR(type->symbol_list, member) {
				printf("\t%s\n", show_ident(member->ident));
			} END_FOR_EACH_PTR(member);
			puts("}");
		}
		break;
	default:
		puts("");
		break;
	}
}

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

		printf("arguments\n");

		FOR_EACH_PTR(sym->arguments, arg) {
			printf("\t\t");
			basetype = arg->ctype.base_type;

			if (basetype->type) {
				printf("%s ", typenames[basetype->type]);
				basetype = basetype->ctype.base_type;
				if (basetype->type)
					printf("%s ", typenames[basetype->type]);
				if (basetype->ident)
					printf("%s ", basetype->ident->name);
			}

			printf("%s\n", arg->ident->name);
		} END_FOR_EACH_PTR(arg);
	}
	else
		putchar('\n');
}

void show_exported(struct symbol *sym)
{
	struct symbol *exp;

	// Symbol name beginning with __ksymtab_ is an exported symbol
	//
	if (starts_with(sym->ident->name, "__ksymtab_")) {
		char *symname = malloc((strlen(sym->ident->name)
				- strlen("__ksymtab_")) * sizeof(char));
		int offset = strlen("__ksymtab_");
		int len = strlen(sym->ident->name) - strlen("__ksymtab_");

		symname = substring(sym->ident->name, offset, len, symname);
		printf("exported sym: %s ", symname, sym->namespace);

		// find the internal declaration of the exported symbol.
		//
		if (exp = find_internal_exported(symlist, symname)) {
			struct symbol *basetype = exp->ctype.base_type;

			add_symbol(&exported, exp);

			// If the exported symbol is a C function, print its
			// args.
			//
			if (basetype->type == SYM_FN) {
				printf("\tctype %s ", typenames[SYM_FN]);
				show_args(basetype);
			}

		} else
			printf("Could not find internal source.\n");
	}
}

void process_file()
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

	symlist = sparse_initialize(argc, argv, &filelist);
	show_symbol_list(symlist, "\n");

	FOR_EACH_PTR_NOTAG(filelist, file)  {
		printf("\nfile: %s\n", file);
		symlist = sparse(file);
		process_file();
	} END_FOR_EACH_PTR_NOTAG(file);

	return 0;
}

int starts_with(const char *a, const char *b)
{
    if(strncmp(a, b, strlen(b)) == 0)
    {
        return 1;
    }
    return 0;
}

char * substring(const char* input, int offset, int len, char* dest)
{
    int input_len = strlen (input);
    if(offset + len > input_len)
    {
        return NULL;
    }
    strncpy(dest, input + offset, len);
    return dest;
}
