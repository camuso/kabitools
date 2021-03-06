/* kabi.c
 *
 * Find all the exported symbols in .i file(s) passed as argument(s) on the
 * command line.
 *
 * Copyright (C) 2015  Red Hat Inc.
 * Tony Camuso <tcamuso@redhat.com>
 *
 * This program is free software. You can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *******************************************************************************
 *
 * Relies heavily on the sparse project. See
 * https://www.openhub.net/p/sparse Include files for sparse in Fedora are
 * located in /usr/include/sparse when sparse is built and installed.
 *
 * Also relies on the boost libraries. www.boost.org
 *
 * Method for identifying exported symbols was inspired by the spartakus
 * project:
 * http://git.engineering.redhat.com/git/users/sbairagy/spartakus.git
 *
 * Gather all the exported symbols and recursively descend into the
 * compound types in the arg lists to acquire all information on compound
 * types that can affect the exported symbols.
 *
 * Symbols will be recursively explored and organized as a graph database.
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
#include <ctype.h>
#include <time.h>
#include <sparse/symbol.h>

#include "kabi.h"
#include "kabi-map.h"
#include "checksum.h"

#define STD_SIGNED(mask, bit) (mask == (MOD_SIGNED | bit))
#define STRBUFSIZ 256
#define MAX_SPARSE_ARGS 16

#define NDEBUG
#if !defined(NDEBUG)
#define DBG(x) x
#define RUN(x)
#define prdbg(fmt, ...) \
do { \
       printf(fmt, ##__VA_ARGS__); \
} while (0)
#else
#define DBG(x)
#define RUN(x) x
#define prdbg(fmt, ...)
#endif

/*****************************************************
** Global declarations
******************************************************/

static const char *helptext ="\
\n\
kabi-parser [options] -f filespec \n\
\n\
    Parses \".i\" (intermediate, c-preprocessed) files for exported \n\
    symbols and symbols of structs and unions that are used by the \n\
    exported symbols. \n\
\n\
Command line arguments:\n\
    -f filespec - Required. Specification of .i files to be processed.\n\
                  Full path and wildcard characters are allowed.\n\
    -o outfile  - Optional. Filename for output data file. \n\
                  The default is \"../kabi-data.dat\". \n\
    -x    Optional. Delete the data file before starting. \n\
    -p    Optional. Parser environment, \"tab\" or \"gen\". \n\
          Default is \"tab\", or normal kernel build.\n\
          \"gen\" is for kernels built with __GENKSYMS__ defined.\n\
    -r    Optional. Report status. Minor problems can interrupt a build.\n\
    -S    Optional. Command line arguments for the sparse semantic parser.\n\
    -h    This help message.\n\
\n\
Example: \n\
\n\
    kabi-parser -p gen -xo ../foo.dat -f foo.i -S -Wall_off \n\
\n\
    * Parser for kernel built with __GENKSYMS__ defined.\n\
    * Sets output file path to ../foo.dat and deletes it first if it already\n\
      exists.\n\
    * Sets the input file path to ./foo.i\n\
    * Sends the \"-Wall_off\" option to the sparse semantic parser.\n\
\n";

enum pfxindex {
	PFX_KSYMTAB,
	PFX_GENKSYM,
	PFX_SIZE,
};

struct pfxentry {
	const char *key;
	const char *pfx;
}
pfxtab[] = {	{"tab", "__ksymtab_"},
		{"gen", "EXPORT_"},
};

static enum pfxindex pfxidx = PFX_KSYMTAB;
static const char *ksymprefix;
static bool kp_rmfiles = false;
static bool cumulative = false;
static struct symbol_list *symlist = NULL;
static bool kabiflag = false;
static char *datafilename = "../kabi-data.dat";
static char *infilespec;
static FILE *inputfile;
static char *sparseargv[MAX_SPARSE_ARGS];
static char **spargvp = &sparseargv[0];
static int sparseargc = 1;
static bool report = false;

/*****************************************************
** sparse wrappers
******************************************************/

static inline void add_string(struct string_list **list, char *string)
{
	// Need to use "notag" call, because bits[1:0] are reserved
	// for tags of some sort. Pointers to strings do not necessarily
	// have the low two bits clear, so this "notag" call is provided
	// for that situation.
	add_ptr_list(list, string);
}

static inline int string_list_size(struct string_list *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

/*****************************************************
** Output formatting
******************************************************/

#if !defined(NDEBUG)
static char *pad_out(int padsize, char padchar)
{
	static char buf[STRBUFSIZ];
	memset(buf, 0, STRBUFSIZ);
	while(padsize--)
		buf[padsize] = padchar;
	return buf;
}
#endif

/*****************************************************
** sparse probing and mining
******************************************************/

//----------------------------------------------------
// Forward Declarations
//----------------------------------------------------
static bool is_exported(struct symbol *sym);
static void get_declist(struct sparm *sp, struct symbol *sym);
static struct symbol *find_internal_exported (struct symbol_list *symlist,
					      char *symname);
static void get_symbols(struct sparm *parent,
			struct symbol_list *list,
			enum ctlflags flags);
//-----------------------------------------------------

static void proc_symlist(struct sparm *parent,
			 struct symbol_list *list,
			 enum ctlflags flags)
{
	struct symbol *sym;
	FOR_EACH_PTR(list, sym) {
		get_symbols(parent, sym->symbol_list, flags);
	} END_FOR_EACH_PTR(sym);
}

static void get_declist(struct sparm *sp, struct symbol *sym)
{
	struct symbol *basetype = sym->ctype.base_type;
	const char *typnam = "\0";

	if (! basetype)
		return;

	if (basetype->type) {
		enum typemask tm = 1 << basetype->type;
		typnam = get_type_name(basetype->type);

		if (basetype->type == SYM_BASETYPE)
			typnam = show_typename(basetype);

		if (basetype->type == SYM_PTR)
			sp->flags |= CTL_POINTER;
		else
			kb_add_to_decl(sp, (char *)typnam);

		if (tm & (SM_STRUCT | SM_UNION))
			sp->flags |= CTL_STRUCT;

		if (basetype->symbol_list) {
			add_symbol((struct symbol_list **)&sp->symlist,
				    basetype);
			sp->flags |= CTL_HASLIST;
		}

		if (tm & SM_FN)
			sp->flags |= CTL_FUNCTION;
	}

	if (basetype->ident)
		kb_add_to_decl(sp, basetype->ident->name);

	get_declist(sp, basetype);
}

static void get_symbols	(struct sparm *parent,
			 struct symbol_list *list,
			 enum ctlflags flags)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {

		struct sparm *sp = kb_new_sparm(parent, flags);
		get_declist(sp, sym);

		// We are only interested in grouping identical compound
		// data types, so we will only create a crc for their type,
		// e.g. "struct foo". For base types and functions, we must
		// include the name (identifier) in the crc as well, or
		// there will be no distinction among them.
		// If there is no identifier, and it's a struct, then set
		// the anonymous flag and recalculate the parent crc using
		// this declaration and its name.
		//
		if (sym->ident) {
			sp->name = sym->ident->name;
			if (!(sp->flags & CTL_STRUCT))
				sp->decl = kb_cstrcat(sp->decl, sp->name);
		} else if (sp->flags & CTL_STRUCT)
			sp->flags |= CTL_ANON;

		// If it's not a struct or union, then we are not interested
		// in its symbol list.
		if (!(sp->flags & CTL_STRUCT))
			sp->flags &= ~CTL_HASLIST;

		kb_init_crc(sp->decl, sp, parent);
#ifndef NDEBUG
		//if (qn->name && ((strstr(qn->name, "st_shndx") != NULL)))
		// if ((sp->crc == 1622272652))// || (parent->crc == 410729264))
		if (sp->name && ((strstr(sp->name, "d_name") != NULL)))
			puts(sp->decl);
#endif
		if (parent->crc == sp->crc)
			sp->flags |= CTL_BACKPTR;

		else if (!(sp->flags & CTL_ANON) &&
			 ((sp->flags & CTL_HASLIST) && (kb_is_dup(sp)))) {
			 sp->flags &= ~CTL_HASLIST;
			 sp->flags |= CTL_ISDUP;
		}

		prdbg("%s%s", pad_out(sp->level, '|'), sp->decl);
		prdbg(" %s\n", sp->name ? sp->name : "");
		kb_update_nodes(sp, parent);

		if ((sp->flags & CTL_HASLIST) && !(sp->flags & CTL_BACKPTR))
			proc_symlist(sp, (struct symbol_list *)sp->symlist,
				     CTL_NESTED);
	} END_FOR_EACH_PTR(sym);
}


static void process_return(struct symbol *basetype, struct sparm *parent)
{
	struct sparm *sp = kb_new_sparm(parent, CTL_RETURN);

	get_declist(sp, basetype);
	kb_init_crc(sp->decl, sp, parent);
	prdbg(" RETURN: %s\n", sp->decl);
	kb_update_nodes(sp, parent);

	if (sp->flags & CTL_HASLIST)
		proc_symlist(sp, (struct symbol_list *)sp->symlist, CTL_NESTED);
}

static void process_exported_struct(struct sparm *sp, struct sparm *parent)
{
	sp->flags |= CTL_EXPSTRUCT;
	kb_init_crc(sp->decl, sp, parent);
	kb_update_nodes(sp, parent);

	if (sp->flags & CTL_HASLIST)
		proc_symlist(sp, (struct symbol_list *)sp->symlist, CTL_NESTED);
}

static void build_branch(struct symbol *sym, struct sparm *parent)
{
	DBG(const char *decl;)
	struct symbol *basetype = sym->ctype.base_type;
	struct sparm *sp = kb_new_sparm(parent, CTL_EXPORTED);
#ifndef NDEBUG
	char *symname = sym->ident->name;
	if (strstr(symname, "bio_set") != NULL)
		printf("symbolname: %s\n", symname);
#endif
	sp->name = sym->ident->name;
	kabiflag = true;
	get_declist(sp, sym);

	// If this is an exported struct or union, we need the decl
	// to correctly calculate the crc.
	DBG(decl = kb_get_decl(sp);)
	prdbg(" EXPORTED: %s %s\n", sp->decl, sp->name);

#ifndef NDEBUG
	if (strstr(decl, "bio_set") != NULL)
		printf("%s %s\n", sp->decl, sp->name);
#endif
	if (!(sp->flags & CTL_FUNCTION)) {
		process_exported_struct(sp, parent);
		return;
	}

	kb_init_crc(sp->name, sp, parent);
	kb_update_nodes(sp, parent);

	if (sp->flags & CTL_HASLIST)
		process_return(basetype, sp);

	if (basetype->arguments)
		get_symbols(sp, basetype->arguments, CTL_ARG);
}

static inline bool begins_with(const char *a, const char *b)
{
	return (strncmp(a, b, strlen(b)) == 0);
}

static inline bool is_valid_basetype(struct symbol *sym)
{
	switch (sym->type) {
	case SYM_BASETYPE:
	case SYM_PTR:
	case SYM_FN:
	case SYM_ARRAY:
	case SYM_STRUCT:
	case SYM_UNION:
		return true;
	default:
		return false;
	}
}

/******************************************************************************
 * PFX_KSYMTAB:
 * find_internal_exported (symbol_list* symlist, char *symname)
 *
 * Search the symbol_list for a symbol->ident->name that matches the
 * symname argument. Once its found and we know it's a valid base_type,
 * return the symbol pointer, else return NULL.
 *
 */
static struct symbol *find_internal_exported (struct symbol_list *symlist,
					      char *symname)
{
	struct symbol *sym = NULL;

	FOR_EACH_PTR(symlist, sym) {

		if (sym && sym->ident &&
		    !(strcmp(sym->ident->name, symname)) &&
		    is_valid_basetype(sym->ctype.base_type))
			return sym;

	} END_FOR_EACH_PTR(sym);

	return NULL;
}

/******************************************************************************
 * PFX_KSYMTAB:
 * process_symname(symbol *sym, sparm *parent)
 *
 * For __ksymtab_ processing, exported symbols are identified by the leading
 * "__ksymtab_" string. However, these symbols are the results of the expansion
 * of the EXPORT_SYMBOL macro and contain no other useful information. The
 * "__ksymtab_" string must be stripped off to create the name of the symbol
 * in the symbol list that has the iformation we really want.
 */
static inline void process_symname(struct symbol *sym, struct sparm *parent)
{
	int offset = strlen(ksymprefix);
	char *symname = &sym->ident->name[offset];
	struct symbol *lsym;
	if ((lsym = find_internal_exported(symlist, symname)))
		build_branch(lsym, parent);
}

/******************************************************************************
 * PFX_KSYMTAB:
 * build_tree_ksymtabs(symbol_list *symlist, sparm *parent)
 *
 * Search the symbol_list for symbols that begin with "__ksymtab_", which
 * identifies them as exported. If found, start the processing.
 */
static void build_tree_ksymtabs(struct symbol_list *symlist,
				struct sparm *parent)
{
	struct symbol *sym;

	FOR_EACH_PTR(symlist, sym) {

		if (sym->ident &&
		    begins_with(sym->ident->name, ksymprefix))
			process_symname(sym, parent);

	} END_FOR_EACH_PTR(sym);
}

/******************************************************************************
 * PFX_GENKSYM:
 * is_exported(symbol *sym)
 *
 * If the symbol has a valid basetpe, then search the input file for an
 * EXPORT_SYMBOL line that contains the symbol name.
 *
 */
static bool is_exported(struct symbol *sym)
{
	char *symname = sym->ident->name;

	if (!sym->ident->name || !(is_valid_basetype(sym->ctype.base_type)))
		return false;

	rewind(inputfile);

	while (!feof(inputfile)) {
		char line[512];
		fgets(line, 512, inputfile);

		if (strstr(line, symname) && strstr(line, "EXPORT"))
			return true;
	}

	return false;
}

/******************************************************************************
 * PFX_GENKSYM:
 * build_tree_genksyms(char *file, symbol_list *symlist, sparm *parent)
 *
 * Search the symbol_list for exported symbols identified by being contained
 * in a line having "EXPORT_SYMBOL". When found, start the processing.
 */
static void build_tree_genksyms(char *file,
				struct symbol_list *symlist,
			        struct sparm *parent)
{
	struct symbol *sym;

	inputfile = fopen(file, "r");

	FOR_EACH_PTR(symlist, sym) {

		if (sym->ident && is_exported(sym))
			build_branch(sym, parent);

	} END_FOR_EACH_PTR(sym);

	fclose(inputfile);
}

/*****************************************************
** Command line option parsing
******************************************************/

static bool set_pfx(const char *key)
{
	int index;
	for (index = 0; index < PFX_SIZE; ++index) {
		if (!(strcmp(key, pfxtab[index].key))) {
			pfxidx = index;
			ksymprefix = pfxtab[index].pfx;
			return true;
		}
	}
	return false;
}

static bool parse_opt(char opt, char ***argv, int *index)
{
	int optstatus = true;

	switch (opt) {
	case 'o' : datafilename = *((*argv)++);
		   ++(*index);
		   break;
	case 'f' : infilespec = *((*argv)++);
		   ++(*index);
		   break;
	case 'c' : cumulative = true;
		   break;
	case 'x' : kp_rmfiles = true;
		   break;
	case 'h' : puts(helptext);
		   exit(0);
	case 'p' : if (!set_pfx(*((*argv)++)))
			optstatus = false;
		   ++(*index);
		   break;
	case 'r' : report = true;
		   break;
	case 'S' : *(++spargvp) = *((*argv)++);
		   ++(*index);
		   ++sparseargc;
	           break;
	default  : optstatus = false;
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

		for (i = 0; argstr[i]; ++i) {
			if (!parse_opt(argstr[i], &argv, &index)) {
				printf ("invalid option: -%c\n", argstr[i]);
				return index;
			}
		}

		if (!*argv)
			break;
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

	DBG(setbuf(stdout, NULL);)

	if (argc <= 1) {
		puts(helptext);
		exit(0);
	}

	ksymprefix = pfxtab[pfxidx].pfx;
	memset((void*)sparseargv, 0, MAX_SPARSE_ARGS * sizeof(char*));
	sparseargv[0] = "sparse_initialize";
	argindex = get_options(&argv[1]);
	argv[argindex] = argv[0];
	argv += argindex;
	argc -= argindex;
	sparseargv[sparseargc] = infilespec;
	++sparseargc;

	if (cumulative) {
		kb_restore_dnodemap(datafilename);
		remove(datafilename);
	}

	symlist = sparse_initialize(sparseargc, sparseargv, &filelist);

	FOR_EACH_PTR_NOTAG(filelist, file) {
		struct sparm *sp = kb_new_firstsparm(file);
		prdbg("sparse file: %s\n", file);
		symlist = __sparse(file);

		if (pfxidx == PFX_KSYMTAB)
			build_tree_ksymtabs(symlist, sp);
		else
			build_tree_genksyms(file, symlist, sp);

	} END_FOR_EACH_PTR_NOTAG(file);

	if (report && !kabiflag)
		return 1;

	if (kp_rmfiles)
		remove(datafilename);

	kb_write_dnodemap(datafilename);
	DBG(kb_dump_dnodemap(datafilename);)

	return 0;
}
