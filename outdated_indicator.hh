#ifndef __OUTDATED_NOTIFIER_HH__
#define __OUTDATED_NOTIFIER_HH__

// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

// Allow clients to find out when something changes.
// The 'something' has an outdated_indicator_factory,
// and calls note_change() when changes are made.
// The client is provided with an outdated_indicator made
// from that factory, which will become outdated after
// further changes are made to the something.

// The default indicator is always outdated.

// When a factory is destroyed, all indicators made from
// that factory become outdated.

#include <boost/shared_ptr.hpp>

class outdated_indicator_factory_impl;

class outdated_indicator
{
  boost::shared_ptr<outdated_indicator_factory_impl> parent;
  unsigned int when;
public:
  outdated_indicator();
  explicit outdated_indicator(boost::shared_ptr<outdated_indicator_factory_impl> p);
  bool outdated();
};


class outdated_indicator_factory
{
  boost::shared_ptr<outdated_indicator_factory_impl> impl;
public:
  outdated_indicator_factory();
  ~outdated_indicator_factory();
  outdated_indicator get_indicator();
  void note_change();
};

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

