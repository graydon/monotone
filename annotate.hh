#ifndef __ANNOTATE_HH__
#define __ANNOTATE_HH__

// copyright (C) 2005 emile snyder <emile@alumni.reed.edu>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <deque>
#include <set>

#include <boost/shared_ptr.hpp>


// imagine the following scenario (. is unknown, x is assigned):
//               A lineage map: [0,11)  (coordinates in A)
//                              [4,15)  (coordinates of A's region in file of interest)
//                 copy to parent A
//                   /---------\                                                 .
// annotations:  ............xxxxxx...............xxxxxxxxx  child rev X
//               \_____/                    \________/
//              copy to p. B               copy to p. B
//                B lineage map: [0, 7)[7,17)
//                               [0, 7)[27,37)
//
// in this case, the region        |+++++++| should get assigned to X, giving new
//
// annotations:  ............xxxxxxXXXXXXXXX......xxxxxxxxx  at the next level
//
// Note that we can't know this without reference to the delta's to *each* parent.
//
// Then a copy from A [2,6) would really be from [6,10), similarly:
//      a copy from B [6,10) would really be from [6,7)[27,30)

typedef std::pair<off_t, off_t> block;

struct lt_block {
  bool operator()(const block &lhs, const block &rhs) { 
    return (lhs.first < rhs.first);
  }
};


// the file that we're annotating is a block of bytes [0, n)
// this represents our knowledge of the annotations as a set of 
// adjacent regions, ie. [j, k) , with an associated revision id 
// or UNKNOWN indicator.
// 
// the file must be wholey specified; ie. if we have regions a, b, c
// then a.i == 0, a.j == b.i, b.j == c.i, and c.j == n
//
class annotate_context {
public:
  annotate_context(file_id fid, app_state &app);

  /// remember that someone did a copy of this region for future
  /// evaluate() call
  void copy_block(off_t start, off_t end);

  /// using the set of copy_block() data we've recorded to date, find
  /// all the regions that were not copied, and assign them to the given
  /// revision.
  void evaluate(revision_id responsible_revision);

  /// assign all remaining unknown regions to the given revision and set 
  /// our complete status.
  void complete(revision_id initial_revision);

  void set_root_revision(revision_id rrid) { root_revision = rrid; }
  revision_id get_root_revision() { return root_revision; }

  bool is_complete() const;

  void dump() const;

private:
  void pack_blockset(std::set<block, lt_block> &blocks);

  std::set<block, lt_block> pending_copy_blocks;
  std::set< std::pair<block, revision_id> > assigned_blocks;

  revision_id root_revision;
  std::string fdata;
};


struct lineage_block {
  lineage_block(const block &localb, const block &finalb) : local_block(localb), final_block(finalb) {}

  block local_block; // the block in the current file version
  block final_block; // the block in the descendent version 
  // (use [0,0) if it doesn't exist.)
};

struct lt_lineage_block {
  bool operator()(const lineage_block &lhs, const lineage_block &rhs) {
    return (lhs.local_block.first < rhs.local_block.first);
  }
};


/*
 * An annotate_lineage records the set of blocks that make up the file
 * and where they came from (if they did) from it's ultimate descendent
 * (remember, we're walking backwards in time.)
 */
class annotate_lineage {
public:
  annotate_lineage();
  annotate_lineage(const block &initialfileblock);

  //void apply_delta(annotate_node_work &work, file_delta fdelta);

  /// copy and insert are used to build up a new lineage by applying 
  /// reverse deltas to a child lineage.
  void copy(boost::shared_ptr<annotate_context> acp, 
            boost::shared_ptr<annotate_lineage> child, 
            block b);
  void insert(off_t length);

private:
  /// given a block from our version of the file, translate this into
  /// blocks from the ultimate descendent file.  it's a set because
  /// a single logical block for us might be several disjoint blocks 
  /// from the original (and some blocks which don't come from the original
  /// at all.
  std::set<lineage_block, lt_lineage_block> translate_block(block b);

  /// used as we build up a file representation with 
  /// copy and insert calls
  off_t current_len;

  std::set<lineage_block, lt_lineage_block> blocks;
};


// a set of data that specifies the input data needed to process 
// the annotation for a given childrev -> parentrev edge.
class annotate_node_work {
public:
  annotate_node_work (boost::shared_ptr<annotate_context> annotations_,
                      boost::shared_ptr<annotate_lineage> lineage_,
                      revision_id node_revision_, file_id node_fid_, file_path node_fpath_)
    : annotations(annotations_), 
      lineage(lineage_),
      node_revision(node_revision_), 
      node_fid(node_fid_), 
      node_fpath(node_fpath_)
  {}

  boost::shared_ptr<annotate_context> annotations;
  boost::shared_ptr<annotate_lineage> lineage;
  revision_id node_revision;
  file_id     node_fid;
  file_path   node_fpath;
};

class annotation_formatter {
};

class annotation_text_formatter : public annotation_formatter {
};


class app_state;

extern void do_annotate_node (const annotate_node_work &workunit, 
                              app_state &app,
                              std::deque<annotate_node_work> &nodes_to_process,
                              std::set<revision_id> &nodes_seen);

extern void write_annotations (boost::shared_ptr<annotate_context> acp, 
                               boost::shared_ptr<annotation_formatter> frmt);

#endif // defined __ANNOTATE_HH__
