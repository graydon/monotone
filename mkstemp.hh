#ifndef __MKSTEMP_HH__
#define __MKSTEMP_HH__

#ifdef HAVE_MKSTEMP

# include <cstdlib>
int inline
monotone_mkstemp(char *tmpl)
{
  return ::mkstemp (tmpl);
}

#else

int
monotone_mkstemp(char *tmpl);

#endif

#endif // __MKSTEMP_HH__
