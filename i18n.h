#ifndef __I18N_H__
#define __I18N_H__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// monotone-specific i18n goo
// include this instead of gettext.h

#include "gettext.h"

#define _(str) gettext(str)
#define N_(str) gettext_noop(str)

void localize_monotone();

#endif
