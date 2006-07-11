#ifndef __OPTIONS_HH__
#define __OPTIONS_HH__

// Copyright (C) 2005 Richard Levitte <richard@levitte.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "popt/popt.h"

#define OPT_DEBUG 1
#define OPT_HELP 2
#define OPT_NOSTD 3
#define OPT_NORC 4
#define OPT_RCFILE 5
#define OPT_DB_NAME 6
#define OPT_KEY_NAME 7
#define OPT_BRANCH_NAME 8
#define OPT_QUIET 9
#define OPT_VERSION 10
#define OPT_DUMP 11
#define OPT_TICKER 12
#define OPT_FULL_VERSION 13
#define OPT_REVISION 14
#define OPT_MESSAGE 15
#define OPT_ROOT 16
#define OPT_DEPTH 17
#define OPT_ARGFILE 18
#define OPT_DATE 19
#define OPT_AUTHOR 20
#define OPT_ALL_FILES 21
#define OPT_PIDFILE 22
#define OPT_MSGFILE 23
#define OPT_BRIEF 24
#define OPT_DIFFS 25
#define OPT_NO_MERGES 26
#define OPT_LAST 27
#define OPT_NEXT 28
#define OPT_VERBOSE 29
#define OPT_SET_DEFAULT 30
#define OPT_EXCLUDE 31
#define OPT_UNIFIED_DIFF 32
#define OPT_CONTEXT_DIFF 33
#define OPT_EXTERNAL_DIFF 34
#define OPT_EXTERNAL_DIFF_ARGS 35
// formerly OPT_LCA was here
#define OPT_EXECUTE 37
#define OPT_KEY_DIR 38
#define OPT_BIND 39
#define OPT_MISSING 40
#define OPT_UNKNOWN 41
#define OPT_KEY_TO_PUSH 42
#define OPT_CONF_DIR 43
#define OPT_DROP_ATTR 44
#define OPT_NO_FILES 45
#define OPT_LOG 46
#define OPT_RECURSIVE 47
#define OPT_REALLYQUIET 48
#define OPT_STDIO 49
#define OPT_NO_TRANSPORT_AUTH 50
#define OPT_SHOW_ENCLOSER 51

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __OPTIONS_HH__
