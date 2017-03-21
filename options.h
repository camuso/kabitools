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
	KB_NODUPS	= 1 << 6,
	KB_ARGS		= 1 << 7,
	KB_QUIET	= 1 << 8,
	KB_MASKSTR	= 1 << 9,
	KB_PATHSTR	= 1 << 10,
	KB_WHITE_LIST	= 1 << 11,
	KB_VERSION	= 1 << 12,
	KB_JUSTONE	= 1 << 13,
};

enum quietlvl {
	QL_0,		// Verbose
	QL_1,		// quieter
	QL_MAX		// quietest
};

class options
{
public:
	options();
	int get_options(int *idx, char **argv,
			std::string &declstr, std::string &datafile,
			std::string &maskstr, std::string &pathstr);
	bool parse_opt(char opt, char ***argv,
		       std::string &declstr, std::string &datafile,
		       std::string &maskstr, std::string &pathstr);
	bool parse_long_opt(char *argstr);
	void bump_qietlvl() { if (m_qlvl < QL_MAX) ++m_qlvl; }
	int kb_flags;

private:
	std::string longopts[OPT_COUNT];
	int m_qlvl = QL_0;
};

#endif // OPTIONS_H
