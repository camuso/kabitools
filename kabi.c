/* kabi.c
 *
 * Find all the exported symbols in .i file(s) passed as argument(s) on the
 * command line when invoking this executable.
 *
 * Copyright (C) 2014  Red Hat Inc.
 * Tony Camuso <tcamuso@redhat.com>
 *
 *******************************************************************************
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
#include <stdint.h>
#include <time.h>
//#define NDEBUG	// comment out to enable asserts
#include <assert.h>

#include <sparse/symbol.h>

#include "kabi.h"
#include "kabi-node.h"
#include "checksum.h"

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
    -d file   optional filename for data file containing the kabi graph. \n\
              The default is \"../kabi-data.dat\". \n\
    -x        Delete the data file before starting. \n\
    -h        This help message.\n\
\n";

static const char *ksymprefix = "__ksymtab_";
static bool kp_verbose = true;
static bool kp_rmfiles = false;
static bool showusers = false;
DBG(static int hiwater = 0;)
static struct symbol_list *symlist = NULL;
static bool kabiflag = false;

static char *datafilename = "../kabi-data.dat";

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
** Output formatting
******************************************************/

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
static void get_declist(struct qnode *qn, struct symbol *sym);
static void get_symbols(struct qnode *qn,
			struct symbol_list *list,
			enum ctlflags flags);

static void proc_symlist(struct qnode *qparent,
			 struct symbol_list *list,
			 enum ctlflags flags)
{
	struct symbol *sym;
	FOR_EACH_PTR(list, sym) {
		get_symbols(qparent, sym->symbol_list, flags);
	} END_FOR_EACH_PTR(sym);
}

static void get_declist(struct qnode *qn, struct symbol *sym)
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

		if (basetype->type == SYM_PTR)
			qn->flags |= CTL_POINTER;
		else
			qn_add_to_declist(qn, (char *)typnam);

		if ((tm & (SM_STRUCT | SM_UNION))
		   && (basetype->symbol_list)) {
			add_symbol((struct symbol_list **)&qn->symlist,
				    basetype);
			qn->flags |= CTL_STRUCT | CTL_HASLIST;
		}

		if (tm & SM_FN)
			qn->flags |= CTL_FUNCTION;
	}

	if (basetype->ident) {
		qn->typnam = basetype->ident->name;
		qn_add_to_declist(qn, basetype->ident->name);
	} else
		qn->typnam = (char *)typnam;

	get_declist(qn, basetype);
}

static void get_symbols	(struct qnode *qparent,
			 struct symbol_list *list,
			 enum ctlflags flags)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		const char *decl;
		unsigned long crc;
		struct qnode *qn = new_qnode(qparent, flags);

		get_declist(qn, sym);
		decl = qn_extract_type(qn);
		crc = raw_crc32(decl);
		qn->cn->crc = crc;

		// DEBUG CODE - creates a breakpoint for the debugger
		// based on the decl content
		if (strstr(decl, "exception_table_entry"))
			get_declist(qn, sym);

		if (qparent->cn->crc == crc)
			qn->flags |= CTL_BACKPTR;
		else if ((qn->flags & CTL_HASLIST)
			 && qn_is_dup(qn, qparent, crc))
			continue;

		if (sym->ident)
			qn->name = sym->ident->name;

		update_qnode(qn);
		printf("%s%s %s\n", pad_out(qn->cn->level, ' '), decl, qn->name);

		if ((qn->flags & CTL_HASLIST) && !(qn->flags & CTL_BACKPTR))
			proc_symlist(qn, (struct symbol_list *)qn->symlist,
				     CTL_NESTED);
	} END_FOR_EACH_PTR(sym);
}

static void build_branch(char *symname, char *file)
{
	struct symbol *sym;

	if ((sym = find_internal_exported(symlist, symname))) {
		const char *decl;
		struct symbol *basetype = sym->ctype.base_type;
		struct qnode *qn = new_firstqnode(CTL_EXPORTED);

		qn->file = file;
		qn->name = symname;
		qn->cn->level = 1;
		kabiflag = true;
		get_declist(qn, sym);
		decl = cstrcat(qn_extract_type(qn), qn->name);
		qn->cn->crc = raw_crc32(decl);

		printf("EXPORTED: %s\n", decl);

		if (qn->flags & CTL_HASLIST) {
			struct qnode *bqn = new_qnode(qn, CTL_RETURN);

			get_declist(bqn, basetype);
			decl = qn_extract_type(bqn);
			bqn->cn->crc = raw_crc32(decl);
			printf("RETURN: %s\n", decl);
			update_qnode(bqn);
		}

		if (basetype->arguments)
			get_symbols(qn, basetype->arguments, CTL_ARG);

		update_qnode(qn);
	}
}

static inline bool begins_with(const char *a, const char *b)
{
	return (strncmp(a, b, strlen(b)) == 0);
}

static void build_tree(struct symbol_list *symlist, char *file)
{
	struct symbol *sym;

	FOR_EACH_PTR(symlist, sym) {

		if (sym->ident &&
		    begins_with(sym->ident->name, ksymprefix)) {
			int offset = strlen(ksymprefix);
			char *symname = &sym->ident->name[offset];
			build_branch(symname, file);
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
static struct symbol *find_internal_exported (struct symbol_list *symlist,
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
** Command line option parsing
******************************************************/

static bool parse_opt(char opt, char ***argv, int *index)
{
	int optstatus = 1;

	switch (opt) {
	case 'd' : datafilename = *((*argv)++);
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

	argindex = get_options(&argv[1]);
	argv += argindex;
	argc -= argindex;

	DBG(puts("got the files");)
	symlist = sparse_initialize(argc, argv, &filelist);

	DBG(puts("created the symlist");)

	FOR_EACH_PTR_NOTAG(filelist, file) {
		DBG(printf("sparse file: %s\n", file);)
		symlist = sparse(file);
		build_tree(symlist,file);
	} END_FOR_EACH_PTR_NOTAG(file);

	DBG(printf("\nhiwater: %d\n", hiwater);)

	if (! kabiflag)
		return 1;

	if (kp_rmfiles)
		remove(datafilename);

	kb_write_qlist(datafilename);
	kb_dump_qlist(datafilename);

	return 0;
}
