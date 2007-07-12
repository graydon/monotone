#ifndef CONFIG_H
#define CONFIG_H 1

#define PACKAGE "monotone"
#define PACKAGE_STRING "monotone 0.35"
#define PACKAGE_BUGREPORT "monotone-devel@nongnu.org"
#define LC_MESSAGES LC_CTYPE
#define VERSION "0.35"

#ifdef _MSC_VER
typedef unsigned long pid_t;
typedef unsigned int os_err_t;
// #define HAVE_EXTERN_TEMPLATE
#define LOCALEDIR ""
#endif

/* Type to use for `s16'. */
#define TYPE_S16 short

/* Type to use for `s32'. */
#define TYPE_S32 int

/* Type to use for `s64'. */
#define TYPE_S64 long long

/* Type to use for `s8'. */
#define TYPE_S8 char

/* Type to use for `u16'. */
#define TYPE_U16 unsigned short

/* Type to use for `u32'. */
#define TYPE_U32 unsigned int

/* Type to use for `u64'. */
#define TYPE_U64 unsigned long long

/* Type to use for `u8'. */
#define TYPE_U8 unsigned char

#endif /* CONFIG_H */
