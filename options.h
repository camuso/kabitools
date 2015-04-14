#ifndef OPTIONS_H
#define OPTIONS_H

enum longopt {
	OPT_NODUPS,
	OPT_ARGS,
	OPT_LIST,
	OPT_DIR,
	OPT_COUNT
};

enum stropts {
	STR_DECL,
	STR_LIST,
	STR_FILE,
	STR_DIR,
	STR_COUNT
};

enum kbflags {
	KB_COUNT	= 1 << 0,
	KB_DECL		= 1 << 1,
	KB_EXPORTS	= 1 << 2,
	KB_STRUCT	= 1 << 3,
	KB_VERBOSE	= 1 << 4,
	KB_WHOLE_WORD	= 1 << 5,
	KB_DATABASE	= 1 << 6,
	KB_NODUPS	= 1 << 7,
	KB_ARGS		= 1 << 8,
	KB_QUIET	= 1 << 9,
	KB_LIST		= 1 << 10,
	KB_DIR		= 1 << 11,
	KB_FILE		= 1 << 12,
};

class options
{
public:
	options();
	bool check_flags();
	int count_bits(unsigned mask);
	bool parse_long_opt(char *argstr, char ***argv);
	bool parse_opt(char opt, char ***argv);
	int get_options(int *idx, char **argv);
	int kb_flags;
	int kb_exemask = KB_COUNT | KB_DECL | KB_EXPORTS | KB_STRUCT;

private:
	std::string longopts[OPT_COUNT];
	std::string strparms[STR_COUNT];

};

#endif // OPTIONS_H
