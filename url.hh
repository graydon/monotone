#ifndef __URL_HH__
#define __URL_HH__

#include <string>

struct utf8;
struct ace;
struct urlenc;

bool parse_utf8_url(utf8 const & utf,
		    utf8 & proto,
		    utf8 & user,
		    utf8 & host,	       
		    utf8 & path,
		    utf8 & group,
		    unsigned long & port);

bool parse_url(url const & u,
	       std::string & proto,
	       ace & user,
	       ace & host,	       
	       urlenc & path,
	       ace & group,
	       unsigned long & port);

#endif // __URL_HH__
