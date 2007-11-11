#ifndef CONFIG_H
#define CONFIG_H 1

#define PACKAGE "monotone"
#define PACKAGE_STRING "monotone 0.37"
#define PACKAGE_BUGREPORT "monotone-devel@nongnu.org"
#define LC_MESSAGES LC_CTYPE
#define VERSION "0.37"

#ifdef _MSC_VER
typedef unsigned long pid_t;
typedef unsigned int os_err_t;
// #define HAVE_EXTERN_TEMPLATE
#define LOCALEDIR ""
#endif

/* Define if using bundled pcre */
#define PCRE_STATIC 1

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

/* MS VC outputs warnings for any function that Microsoft has decided
   is unsafe, like strcpy, strlen, getenv, open, strdup and so on.
   They want you to replace any of those calls with calls to the new
   Microsoft specific "safe" versions, such as sopen_s or dupenv_s.
   The problem with that is that no other platform has these functions,
   so if you maintain a multi-platform source base, you're screwed.
   The only real way around it is to either wrap all these functions
   into your out OS neutral api, or use #defines to remap the calls.
   Or, do the following to suppress those warnings, since they aren't
   likely to get fixed the way Microsoft wants, and there are other
   functions that are more widely accepted.
*/
#define _CRT_SECURE_NO_WARNINGS

#endif /* CONFIG_H */
