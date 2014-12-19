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

static void clean_up_symbols(struct symbol_list *list)
{
    printf("%s():\n", __func__);
    struct symbol *sym;

    FOR_EACH_PTR(list, sym)
    {
	expand_symbol(sym);

    } END_FOR_EACH_PTR(sym);
}

static void prsym(struct symbol *sym)
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
struct symbol *find_internal_exported(struct symbol_list *symlist, char *symname)
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

void prargs (struct symbol *sym)
{
    if (sym->arg_count)
	    printf("\targ_count: %d ", sym->arg_count);

    if (sym->arguments) {
	struct symbol *arg = NULL;

	printf("arguments\n");

	FOR_EACH_PTR(sym->arguments, arg) {
	    printf("\t\t%s\n", arg->ident->name);
	} END_FOR_EACH_PTR(arg);
    }
    else
	putchar('\n');
}

int main(int argc, char **argv)
{
    struct symbol_list *symlist = NULL;
    struct symbol_list *exported = NULL;
    struct string_list *filelist = NULL;
    char *file;
    struct symbol *sym;
    struct symbol *exp;

    symlist = sparse_initialize(argc, argv, &filelist);
    show_symbol_list(symlist, "\n");

    FOR_EACH_PTR_NOTAG(filelist, file)  {
	printf("\nfile: %s\n", file);
	symlist = sparse(file);

        FOR_EACH_PTR(symlist, sym) {

            // Symbol name beginning with __ksymtab_ is an exported symbol
	    //
            if (starts_with(sym->ident->name, "__ksymtab_")) {
                char *symname;
                symname = malloc((strlen(sym->ident->name)
			- strlen("__ksymtab_")) * sizeof(char));
                int offset = strlen("__ksymtab_");
                int len = strlen(sym->ident->name) - strlen("__ksymtab_");
                symname = substring(sym->ident->name, offset, len, symname);
                printf("exported sym: %s ", symname, sym->namespace);

		// find the symbol that corresponds to this exported symbol.
		//
		exp = find_internal_exported(symlist, symname);
		if (exp) {
		    struct symbol *basetype = exp->ctype.base_type;

		    add_symbol(&exported, exp);

		    if (exp->type == SYM_FN) {
			printf("\ttype FUNCTION ");
			prargs(exp);
		    }

		    if (basetype->type == SYM_FN) {
			printf("\tctype FUNCTION ");
			prargs(basetype);
		    }

		} else
		    printf("Could not find internal source.\n");
            }
        }END_FOR_EACH_PTR(sym);
    }END_FOR_EACH_PTR_NOTAG(file);
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
