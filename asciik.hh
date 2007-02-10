#ifndef __ASCIIK_HH__
#define __ASCIIK_HH__

#include <set>
#include <vector>

#include "revision.hh"

class asciik
{
public:
  asciik(size_t min_width = 0, std::ostream & os = std::cout);
  // Prints an ASCII-k chunk using the given revisions.
  void print(const revision_id & rev, const std::set<revision_id> & parents,
    const string & annotation);
  //TODO: cambiare set-parents to vector-next
private:
  void links_cross(const std::set<std::pair<size_t, size_t> > & links,
    std::set<size_t> & crosses) const;
  void draw(const size_t curr_items, const size_t next_items,
    const size_t curr_loc, const std::set<std::pair<size_t, size_t> > & links,
    const std::set<size_t> & curr_ghosts, const string & annotation) const;
  bool try_draw(const std::vector<revision_id> & next_row,
    const size_t curr_loc, const std::set<revision_id> & parents,
    const string & annotation) const;
  // internal state
  size_t width;
  std::ostream * output;
  std::vector<revision_id> curr_row;
};

#endif
