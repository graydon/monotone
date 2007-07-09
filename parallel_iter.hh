#ifndef __PARALLEL_ITER_HH__
#define __PARALLEL_ITER_HH__

// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// An ugly, but handy, little helper class for doing parallel iteration
// over maps.
// Usage:
//   parallel::iter<std::map<foo, bar> > i(left_map, right_map);
//   while (i.next())
//     switch (i.state())
//     {
//     case parallel::invalid:
//       I(false);
//     case parallel::in_left:
//       // use left_value(), left_key(), left_data()
//       break;
//     case parallel::in_right:
//       // use right_value(), right_key(), right_data()
//       break;
//     case parallel::in_both:
//       // use left_value(), right_value(), left_key(), right_key(),
//       // left_data(), right_data()
//       break;
//     }
//
// This code would make Alexander Stepanov cry; not only is it only defined
// for maps, but it will only work on maps that use the default (std::less)
// sort order.


#include "lexical_cast.hh"

#include "sanity.hh"

namespace parallel
{
  typedef enum { in_left, in_right, in_both, invalid } state_t;

  template <typename M>
  class iter
  {
  public:
    M const & left_map;
    M const & right_map;

    iter(M const & left_map, M const & right_map)
      : left_map(left_map), right_map(right_map),
        state_(invalid), started_(false), finished_(false)
    {
    }

    bool next()
    {
      I(!finished_);
      // update iterators past the last item returned
      if (!started_)
        {
          left_ = left_map.begin();
          right_ = right_map.begin();
          started_ = true;
        }
      else
        {
          I(state_ != invalid);
          if (state_ == in_left || state_ == in_both)
            ++left_;
          if (state_ == in_right || state_ == in_both)
            ++right_;
        }
      // determine new state
      I(started_);
      if (left_ == left_map.end() && right_ == right_map.end())
        {
          finished_ = true;
          state_ = invalid;
        }
      else if (left_ == left_map.end() && right_ != right_map.end())
        state_ = in_right;
      else if (left_ != left_map.end() && right_ == right_map.end())
        state_ = in_left;
      else
        {
          // Both iterators valid.
          if (left_->first < right_->first)
            state_ = in_left;
          else if (right_->first < left_->first)
            state_ = in_right;
          else
            {
              I(left_->first == right_->first);
              state_ = in_both;
          }
        }
      return !finished_;
    }

    state_t state() const
    {
      return state_;
    }

    typename M::value_type const &
    left_value()
    {
      I(state_ == in_left || state_ == in_both);
      return *left_;
    }

    typename M::key_type const &
    left_key()
    {
      return left_value().first;
    }

    typename M::value_type::second_type const &
    left_data()
    {
      return left_value().second;
    }

    typename M::value_type const &
    right_value()
    {
      I(state_ == in_right || state_ == in_both);
      return *right_;
    }

    typename M::key_type const &
    right_key()
    {
      return right_value().first;
    }

    typename M::value_type::second_type const &
    right_data()
    {
      return right_value().second;
    }

  private:
    state_t state_;
    bool started_, finished_;
    typename M::const_iterator left_;
    typename M::const_iterator right_;

  };

  template <typename M> void
  dump(iter<M> const & i, std::string & out)
  {
    out = boost::lexical_cast<std::string>(i.state());
    switch (i.state())
      {
      case in_left: out += " in_left"; break;
      case in_right: out += " in_right"; break;
      case in_both: out += " in_both"; break;
      case invalid: out += " invalid"; break;
      }
    out += "\n";
  }

}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
