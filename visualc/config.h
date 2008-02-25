#ifndef CONFIG_H
#define CONFIG_H 1

#define PACKAGE "monotone"
#define PACKAGE_STRING "monotone 0.39"
#define PACKAGE_BUGREPORT "monotone-devel@nongnu.org"
#define LC_MESSAGES LC_CTYPE
#define VERSION "0.39"

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

/*
 * Disable MS specific warning C4290:
 * C++ exception specification ignored except to indicate a function is not __declspec(nothrow)
 *
 * A function is declared using exception specification, which Visual C++ accepts but does not implement. 
 * Code with exception specifications that are ignored during compilation may need to be recompiled and 
 * linked to be reused in future versions supporting exception specifications.
 */
#pragma warning( disable : 4290 )


/*
 * Disable MS specific warning C4250:
 * Two or more members have the same name. The one in class2 is inherited because it is a base class
 * for the other classes that contained this member.
 * Because a virtual base class is shared among multiple derived classes, a name in a derived class 
 * dominates a name in a base class. 
 * For example, given the following class hierarchy, there are two definitions of func inherited within diamond: 
 * the vbc::func() instance through the weak class, and the dominant::func() through the dominant class. 
 * An unqualified call of func() through a diamond class object, always calls the dominate::func() instance. 
 * If the weak class were to introduce an instance of func(), neither definition would dominate, and the call would be flagged as ambiguous.
 */
#pragma warning( disable : 4250 )


#endif /* CONFIG_H */
