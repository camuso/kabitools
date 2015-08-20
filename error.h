#ifndef ERROR_H
#define ERROR_H

// Execution message enumerations
enum exemsg {
	EXE_OK,
	EXE_ARG2BIG,
	EXE_ARG2SML,
	EXE_CONFLICT,
	EXE_BADFORM,
	EXE_INVARG,
	EXE_NOFILE,
	EXE_NOTFOUND,
	EXE_NOTWHITE,
	EXE_NO_WLIST,
	EXE_NODIR,
	EXE_COUNT,
};

enum errfmt {
	EF_0,
	EF_1,
	EF_2,
};

class error
{
public:
	error(){}
	void init (int argc, char **argv);
	void print_cmdline();
	void print_cmd_errmsg(int err, std::string& str1, std::string& str2);
	void print_errmsg(int err, std::vector<std::string> strvec);
	int get_cmderrmask() {return m_cmderrmask;}
	void set_cmderrmask_bit(int bit) {m_cmderrmask |= bit;}
	void clr_cmderrmask_bit(int bit) {m_cmderrmask &= ~bit;}

private:
	void map_err(int err, std::string str);
	int m_orig_argc;
	char **m_orig_argv;
	std::map <int, std::string> m_errmap;
	std::string errstr[EXE_COUNT];

	int m_cmderrmask =
			((1 << EXE_ARG2BIG)  |
			 (1 << EXE_ARG2SML)  |
			 (1 << EXE_CONFLICT) |
			 (1 << EXE_BADFORM)  |
			 (1 << EXE_INVARG));
};

#endif // ERROR_H
