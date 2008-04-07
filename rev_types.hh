// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __REV_TYPES_HH__
#define __REV_TYPES_HH__

// This file contains forward declarations and typedefs for all of the basic
// types associated with revision handling.  It should be included instead
// of (any or all of) basic_io.hh, cset.hh, graph.hh, paths.hh, revision.hh,
// roster.hh, and database.hh when all that is necessary is these
// declarations.

#include <boost/shared_ptr.hpp>
#include "vocab.hh"
#include "numeric_vocab.hh"
#include "hybrid_map.hh"
#include "vector.hh"

// full definitions in basic_io.hh
namespace basic_io
{
  struct printer;
  struct parser;
  struct stanza;
}

// full definitions in cset.hh 
typedef std::map<attr_key, attr_value> attr_map_t;
typedef u32 node_id;
struct cset;
struct editable_tree;

// full definitions in graph.hh
struct rev_graph;
struct reconstruction_graph;
typedef std::vector<id> reconstruction_path;
typedef std::multimap<revision_id, revision_id> rev_ancestry_map;

// full definitions in paths.hh
class any_path;
class bookkeeping_path;
class file_path;
class system_path;
class path_component;

// full definitions in revision.hh
struct revision_t;
typedef std::map<revision_id, boost::shared_ptr<cset> > edge_map;
typedef edge_map::value_type edge_entry;

// full definitions in rev_height.hh
class rev_height;

// full definitions in roster.hh
struct node_id_source;
struct node;
struct dir_node;
struct file_node;
struct marking_t;
class roster_t;
class editable_roster_base;

typedef boost::shared_ptr<node> node_t;
typedef boost::shared_ptr<file_node> file_t;
typedef boost::shared_ptr<dir_node> dir_t;
typedef std::map<node_id, marking_t> marking_map;

typedef std::map<path_component, node_t> dir_map;
typedef hybrid_map<node_id, node_t> node_map;

// (true, "val") or (false, "") are both valid attr values (for proper
// merging, we have to widen the attr_value type to include a first-class
// "undefined" value).
typedef std::map<attr_key, std::pair<bool, attr_value> > full_attr_map_t;

// full definitions in database.hh
class database;
class conditional_transaction_guard;
class transaction_guard;

typedef boost::shared_ptr<roster_t const> roster_t_cp;
typedef boost::shared_ptr<marking_map const> marking_map_cp;
typedef std::pair<roster_t_cp, marking_map_cp> cached_roster;

typedef std::map<revision_id, cached_roster> parent_map;
typedef parent_map::value_type parent_entry;

#endif // __REV_TYPES_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
