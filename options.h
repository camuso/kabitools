#ifndef OPTIONS_H
#define OPTIONS_H

enum longopt {
	OPT_NODUPS,
	OPT_ARGS,
	OPT_COUNT
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
};

class options
{
public:
	options();
	int get_options(int *idx, char **argv,
			std::string &declstr, std::string &datafile);
	bool parse_opt(char opt, char ***argv,
		       std::string &declstr, std::string &datafile);
	bool parse_long_opt(char *argstr);

private:
	std::string longopts[OPT_COUNT];
	int kb_flags;
};

#endif // OPTIONS_H
