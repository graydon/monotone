// this is how you "ask for" the C99 constant constructor macros.  *and*
// you have to do so before any other files accidentally include
// stdint.h. awesome.  (from change_set.cc, required for UINT32_C)
#define __STDC_CONSTANT_MACROS

#ifdef WIN32
#define BOOST_NO_STDC_NAMESPACE
// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif

#include <boost/bind.hpp>
#include <boost/config.hpp>
#include <boost/cstdlib.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
// We have a local version of this to work around a bug in the MSVC debug checking.
#include <boost/function.hpp>
#include "lexical_cast.hh"
#include <boost/optional.hpp>
#include <boost/random.hpp>
#include <boost/scoped_array.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/static_assert.hpp>
#include <boost/tokenizer.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/version.hpp>
