#ifndef KABISERIAL_H
#define KABISERIAL_H

#ifdef __cplusplus
extern "C"
{
#endif
#ifdef __cplusplus
}
#endif
#endif // KABISERIAL_H

enum ctlflags {
	CTL_POINTER 	= 1 << 0,
	CTL_ARRAY	= 1 << 1,
	CTL_STRUCT	= 1 << 2,
	CTL_FUNCTION	= 1 << 3,
	CTL_EXPORTED 	= 1 << 4,
	CTL_RETURN	= 1 << 5,
	CTL_ARG		= 1 << 6,
	CTL_NESTED	= 1 << 7,
	CTL_GODEEP	= 1 << 8,
	CTL_DONE	= 1 << 9,
	CTL_FILE	= 1 << 10,
};
