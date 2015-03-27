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
	EXE_NOTFOUND_SIMPLE,
	EXE_2MANY,
	EXE_COUNT
};

class error
{
public:
	error(){}
	void init (int argc, char **argv);
	void print_cmdline();
	void print_cmd_errmsg(int err,
			      std::string declstr, std::string datafile);
	int get_errmask() {return m_errmask;}
	void set_errmask_bit(int bit) {m_errmask |= bit;}
	void clr_errmask_bit(int bit) {m_errmask &= ~bit;}

private:
	int m_orig_argc;
	char **m_orig_argv;
	std::string errstr[EXE_COUNT];
	int m_errmask =	((1 << EXE_ARG2BIG)  |
			 (1 << EXE_ARG2SML)  |
			 (1 << EXE_CONFLICT) |
			 (1 << EXE_BADFORM)  |
			 (1 << EXE_INVARG));
};

#endif // ERROR_H
