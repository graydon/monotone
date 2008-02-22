#ifndef __ASCIIK_HH__
#define __ASCIIK_HH__

#include <set>
#include "vector.hh"
#include "vocab.hh"

class asciik
{
public:
  asciik(std::ostream & os, size_t min_width = 0);
  // Prints an ASCII-k chunk using the given revisions.
  // Multiple lines are supported in annotation (the graph will stretch
  // accordingly); empty newlines at the end will be removed.
  void print(revision_id const & rev,
             std::set<revision_id> const & parents,
             std::string const & annotation);
  //TODO: change set-of-parents to vector-of-successors
private:
  void links_cross(std::set<std::pair<size_t, size_t> > const & links,
                   std::set<size_t> & crosses) const;
  void draw(size_t const curr_items,
            size_t const next_items,
            size_t const curr_loc,
            std::set<std::pair<size_t, size_t> > const & links,
            std::set<size_t> const & curr_ghosts,
            std::string const & annotation) const;
  bool try_draw(std::vector<revision_id> const & next_row,
                size_t const curr_loc,
                std::set<revision_id> const & parents,
                std::string const & annotation) const;
  // internal state
  size_t width;
  std::ostream & output;
  std::vector<revision_id> curr_row;
};

#endif
