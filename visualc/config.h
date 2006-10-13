#ifndef CONFIG_H
#define CONFIG_H 1

#define PACKAGE "monotone"
#define PACKAGE_STRING "monotone 0.30"
#define PACKAGE_BUGREPORT "monotone-devel@nongnu.org"
#define LC_MESSAGES LC_CTYPE
#define VERSION "0.30"

#ifdef _MSC_VER
typedef unsigned long pid_t;
typedef unsigned int os_err_t;
// #define HAVE_EXTERN_TEMPLATE
#define LOCALEDIR ""
#endif

#endif /* CONFIG_H */
