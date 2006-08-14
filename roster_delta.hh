//////////
// Experimental roster delta stuff

#include <set>
#include <map>

#include "paths.hh"
#include "roster.hh"

struct roster_delta
{
  typedef std::map<node_id> nodes_deleted_t;
  nodes_deleted_t nodes_deleted;
  typedef std::map<std::pair<node_id, path_component>, node_id> dirs_added_t;
  dirs_added_t dirs_added;
  typedef std::map<std::pair<node_id, path_component>, std::pair<node_id, file_id> > files_added_t;
  files_added_t files_added;
  typedef std::map<node_id, std::pair<node_id, path_component> > nodes_renamed_t;
  nodes_renamed_t nodes_renamed;
  typedef std::map<node_id, file_id> deltas_applied_t;
  deltas_applied_t deltas_applied;
  typedef std::set<std::pair<node_id, attr_key> > attrs_cleared_t;
  attrs_cleared_t attrs_cleared;
  typedef std::set<std::pair<node_id, std::pair<attr_key, std::pair<bool, attr_value> > > attrs_changed_t;
  attrs_changed_t attrs_changed;

  // nodes_deleted are automatically removed from the marking_map; these are
  // all markings that are new or changed
  typedef std::map<node_id, marking_t> markings_changed_t;
  markings_changed_t markings_changed;

  void
  apply(roster_t & roster, marking_map & markings) const;
};

void
make_roster_delta(roster_t const & from, marking_map const & from_markings,
                  roster_t const & to, marking_map const & to_markings,
                  roster_delta & d);

void
parse_roster_delta(basic_io::parser & parser, roster_delta & d);
