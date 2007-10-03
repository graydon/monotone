// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#include "base.hh"
#include "outdated_indicator.hh"
#include "sanity.hh"

class outdated_indicator_factory_impl
{
  unsigned int changed;
  unsigned int dispensed;
public:
  outdated_indicator_factory_impl();
  void note_change();
  unsigned int last_change() const;
  unsigned int dispense();
};

outdated_indicator_factory_impl::outdated_indicator_factory_impl()
  : changed(0), dispensed(0)
{}

unsigned int
outdated_indicator_factory_impl::last_change() const
{
  return changed;
}

unsigned int
outdated_indicator_factory_impl::dispense()
{
  I(changed == dispensed || changed == dispensed + 1);
  dispensed = changed;
  return dispensed;
}

void
outdated_indicator_factory_impl::note_change()
{
  I(changed == dispensed || changed == dispensed + 1);
  if (changed == dispensed)
    ++changed;
}


outdated_indicator::outdated_indicator()
  : parent(), when(0)
{}

outdated_indicator::outdated_indicator(boost::shared_ptr<outdated_indicator_factory_impl> p)
  : parent(p), when(p->dispense())
{}

bool
outdated_indicator::outdated()
{
  if (parent)
    {
      I(when <= parent->last_change());
      return when < parent->last_change();
    }
  else
    return true;
}


outdated_indicator_factory::outdated_indicator_factory()
  : impl(new outdated_indicator_factory_impl)
{}

outdated_indicator_factory::~outdated_indicator_factory()
{
  impl->note_change();
}

outdated_indicator
outdated_indicator_factory::get_indicator()
{
  return outdated_indicator(impl);
}

void
outdated_indicator_factory::note_change()
{
  impl->note_change();
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

UNIT_TEST(outdated_indicator, )
{
  outdated_indicator indicator;
  {
    outdated_indicator_factory factory;
    UNIT_TEST_CHECK(indicator.outdated());
    indicator = factory.get_indicator();
    UNIT_TEST_CHECK(!indicator.outdated());
    factory.note_change();
    UNIT_TEST_CHECK(indicator.outdated());
    factory.note_change();
    factory.note_change();
    indicator = factory.get_indicator();
    UNIT_TEST_CHECK(!indicator.outdated());
  }
  UNIT_TEST_CHECK(indicator.outdated());
}

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

