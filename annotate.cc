// copyright (C) 2005 emile snyder <emile@alumni.reed.edu>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <sstream>

#include "platform.hh"
#include "vocab.hh"
#include "sanity.hh"
#include "revision.hh"
#include "change_set.hh"
#include "app_state.hh"
#include "manifest.hh"
#include "transforms.hh"
#include "annotate.hh"


annotate_context::annotate_context(file_id fid, app_state &app)
{
  // get the file data (and store it here?)
  file_data fpacked;
  data funpacked;
  app.db.get_file_version(fid, fpacked);
  //unpack(fpacked.inner(), funpacked);
  funpacked = fpacked.inner();
  fdata = funpacked();
}


void
annotate_context::copy_block(off_t start, off_t end)
{
  L(F("annotate_context::copy_block [%d, %d)\n") % start % end);
  pending_copy_blocks.insert(std::make_pair(start, end));
}


void
annotate_context::evaluate(revision_id responsible_revision)
{
  pack_blockset(pending_copy_blocks);

  // any block that is not either copied or already assigned is assigned to rev.
  // So build a set of all copies and assignments and pack it, then walk over it 
  // to find the gaps.
  std::set<block, lt_block> copied_or_assigned;

  std::set<block, lt_block>::const_iterator i;
  std::set<block, lt_block>::const_iterator next;

  for (i = pending_copy_blocks.begin(); i != pending_copy_blocks.end(); i++) {
    copied_or_assigned.insert(*i);
  }
  std::set< std::pair<block, revision_id> >::const_iterator j;
  for (j = assigned_blocks.begin(); j != assigned_blocks.end(); j++) {
    copied_or_assigned.insert(j->first);
  }
  L(F("packing copied_or_assigned\n"));
  pack_blockset(copied_or_assigned);
  if (copied_or_assigned.size() > 0) {
    i = copied_or_assigned.begin();
    if (i->first > 0) {
      block b(0, i->first);
      L(F("assigning block [%d, %d) <- %s\n") % b.first % b.second % responsible_revision);
      assigned_blocks.insert(std::make_pair(b, responsible_revision));
    }
    next = i;
    next++;
    while (next != copied_or_assigned.end()) {
      I(i != copied_or_assigned.end());
      I(i->second <= next->first);
      if (i->second < next->first) {
        block b(i->second, next->first);
        L(F("assigning block [%d, %d) <- %s\n") % b.first % b.second % responsible_revision);
        assigned_blocks.insert(std::make_pair(b, responsible_revision));
      }
      i++;
      next++;
    }

    if (fdata.size() > i->second) {
      block b(i->second, fdata.size());
      L(F("assigning block [%d, %d) <- %s\n") % b.first % b.second % responsible_revision);
      assigned_blocks.insert(std::make_pair(b, responsible_revision));
    }
  }

  pending_copy_blocks.clear();
}


void
annotate_context::complete(revision_id initial_revision)
{
  if (fdata.size() == 0) 
    return;

  std::set< std::pair<block, revision_id> >::const_iterator i;
  std::set< std::pair<block, revision_id> >::const_iterator next;

  i = assigned_blocks.begin();

  if (i == assigned_blocks.end())
    return;

  if (i->first.first > 0)
    i = assigned_blocks.insert(i, std::make_pair(std::make_pair(0, i->first.first), initial_revision));

  next = i; next++;
  while (next != assigned_blocks.end()) {
    I(i->first.second <= next->first.first);

    if (i->first.second != next->first.first) {
      i = assigned_blocks.insert(i, std::make_pair(std::make_pair(i->first.second, next->first.first), initial_revision));
      next = i;
      next++;
      continue;
    }
        
    i++;
    next++;
  }

  if (i->first.second < fdata.size())
    assigned_blocks.insert(std::make_pair(std::make_pair(i->first.second, fdata.size()), initial_revision));
}


bool
annotate_context::is_complete() const
{
  if (fdata.size() == 0) 
    return true;

  std::set< std::pair<block, revision_id> >::const_iterator i;
  std::set< std::pair<block, revision_id> >::const_iterator next;

  i = assigned_blocks.begin();

  if (i == assigned_blocks.end())
    return false;
  if (i->first.first > 0)
    return false;

  next = i; next++;
  while (next != assigned_blocks.end()) {
    I(i->first.second <= next->first.first);
    if (i->first.second != next->first.first)
      return false;
    i++;
    next++;
  }

  if (i->first.second < fdata.size())
    return false;

  return true;
}


void
annotate_context::dump() const
{
  std::set< std::pair<block, revision_id> >::const_iterator i;
  for (i = assigned_blocks.begin(); i != assigned_blocks.end(); i++) {
    L(F("annotate_context::dump [%d, %d) <- %s\n") % i->first.first % i->first.second % i->second);
  }
}


void
annotate_context::pack_blockset(std::set<block, lt_block> &blocks)
{
  L(F("annotate_context::pack_blockset blocks.size() == %d\n") % blocks.size());

  if (blocks.size() < 2)
    return;

  std::set<block, lt_block>::iterator i, next;
  i = blocks.begin();
  next = i;
  next++;

  while (i != blocks.end() && next != blocks.end()) {
    L(F("annotate_context::pack_blockset test [%d, %d) and [%d, %d) for overlap\n")
      % i->first % i->second % next->first % next->second);

    if (i->second > next->first) {
      L(F("merging\n"));
      if (i->second < next->second) {
        block newb(i->first, next->second);
        L(F("new block is [%d, %d)\n") % newb.first % newb.second);
        blocks.erase(next);
        blocks.erase(i);
        i = blocks.insert(i, newb);
        next = i;
        next++;
        continue;
      } else {
        L(F("next is contained in i, deleting next\n"));
        blocks.erase(next);
        next = i;
        next++;
        continue;
      }
    }

    L(F("incrementing\n"));
    i++;
    next++;
  }
}



annotate_lineage::annotate_lineage()
  : current_len(0)
{
  // do nothing, we'll get built by calls to copy() and insert() using the
  // child lineage
}

annotate_lineage::annotate_lineage(const block &initialfileblock)
  : current_len(initialfileblock.second)
{
  blocks.insert(lineage_block(initialfileblock, initialfileblock));
}

void 
annotate_lineage::copy(boost::shared_ptr<annotate_context> acp,
                       boost::shared_ptr<annotate_lineage> child, block b)
{
  std::set<lineage_block, lt_lineage_block> child_block_view = child->translate_block(b);

  std::set<lineage_block, lt_lineage_block>::const_iterator i;
  for (i=child_block_view.begin(); i != child_block_view.end(); i++) {
    off_t blen = i->local_block.second - i->local_block.first;
    blocks.insert(lineage_block(std::make_pair(current_len, current_len+blen),
                                i->final_block));

    L(F("annotate_lineage::copy now mapping [%d, %d) -> [%d, %d)\n") 
      % current_len % (current_len + blen) % i->final_block.first % i->final_block.second);

    current_len += blen;
    if (i->final_block.second > i->final_block.first)
      acp->copy_block(i->final_block.first, i->final_block.second);
  }
}

void 
annotate_lineage::insert(off_t length)
{
  L(F("annotate::insert called with length %d and current_len %d\n") % length % current_len);

  blocks.insert(lineage_block(std::make_pair(current_len, current_len + length),
                              std::make_pair(0, 0)));

  L(F("annotate_lineage::insert now mapping [%d, %d) -> [0, 0)\n") 
    % current_len % (current_len + length));

  current_len += length;
}

std::set<lineage_block, lt_lineage_block> 
annotate_lineage::translate_block(block b)
{
  I(b.second <= current_len);

  std::set<lineage_block, lt_lineage_block> result;

  std::set<lineage_block, lt_lineage_block>::const_iterator i;
  for (i=blocks.begin(); i != blocks.end(); i++) {
    L(F("annotate_lineage::translate_block b [%d, %d), i [%d, %d) -> [%d, %d)\n") 
      % b.first % b.second % i->local_block.first % i->local_block.second % i->final_block.first % i->final_block.second);

    if (i->local_block.second < b.first) { // b comes after i
      L(F("b after i -- continue\n"));
      continue;
    }

    if (i->local_block.first >= b.second) { // b comes before i
      // local blocks are sorted, so this means no match
      L(F("b before i -- break\n"));
      break;
    }

    // we must have copied all earlier portions of b already
    I(b.first >= i->local_block.first);

    bool final_block_exists = i->final_block.second > i->final_block.first;

    block bc, bf;
    off_t final_delta_start;

    if (b.first > i->local_block.first) {
      bc.first = b.first;
      final_delta_start = b.first - i->local_block.first;
    } else {
      bc.first = i->local_block.first;
      final_delta_start = 0;
    }

    if (b.second < i->local_block.second) {
      bc.second = b.second;
      bf = i->final_block;
      if (final_block_exists) {
        bf.first += final_delta_start;
        bf.second -= i->local_block.second - b.second;
      }

      result.insert(lineage_block(bc, bf));
      break;
    } else {
      bc.second = i->local_block.second;
      bf = i->final_block;
      if (final_block_exists)
        bf.first += final_delta_start;
      result.insert(lineage_block(bc, bf));
      b.first = i->local_block.second;
    }
  }

  return result;
}




boost::shared_ptr<annotate_lineage>
apply_delta_annotation (const annotate_node_work &work, file_delta d)
{
  delta dd;
  //unpack(d.inner(), dd);
  dd = d.inner();
  std::string delta_string = dd();
  L(F("file delta to child %s:\n%s\n") % work.node_revision % dd);

  boost::shared_ptr<annotate_lineage> parent_lineage(new annotate_lineage());

  // parse the delta; consists of C blocks and I blocks
  // patterned on xdelta.cc:apply_delta()

  std::istringstream del(delta_string);
  for (char c = del.get(); c == 'I' || c == 'C'; c = del.get()) {
    I(del.good());
    if (c == 'I') {
      std::string::size_type len = std::string::npos;
      del >> len;
      I(del.good());
      I(len != std::string::npos);
      //string tmp;
      //tmp.reserve(len);
      I(del.get(c).good());
      I(c == '\n');

      parent_lineage->insert(len);

      while (len--) {
        I(del.get(c).good());
        //tmp += c;
      }
      I(del.get(c).good());
      I(c == '\n');
            
      // do our thing with the string tmp?

    } else { // c == 'C'
      std::string::size_type pos = std::string::npos, len = std::string::npos;
      del >> pos >> len;
      I(del.good());
      I(len != std::string::npos);
      I(del.get(c).good());
      I(c == '\n');

      parent_lineage->copy(work.annotations, work.lineage, std::make_pair(pos, pos + len));
    }
  }

  return parent_lineage;
}

file_id
find_file_id_in_revision(app_state &app, file_path fpath, revision_id rid)
{
  // find the version of the file requested
  manifest_map mm;
  revision_set rev;
  app.db.get_revision(rid, rev);
  app.db.get_manifest(rev.new_manifest, mm);
  manifest_map::const_iterator i = mm.find(fpath);
  I(i != mm.end());
  file_id fid = manifest_entry_id(*i);
  return fid;
}

void
do_annotate_node (const annotate_node_work &work_unit, 
                  app_state &app,
                  std::deque<annotate_node_work> &nodes_to_process,
                  std::set<revision_id> &nodes_seen)
{
  L(F("do_annotate_node for node %s\n") % work_unit.node_revision);
  nodes_seen.insert(work_unit.node_revision);
  revision_id null_revision; // initialized to 0 by default?

  // get the revisions parents
  std::set<revision_id> parents;
  app.db.get_revision_parents(work_unit.node_revision, parents);
  L(F("do_annotate_node found %d parents for node %s\n") % parents.size() % work_unit.node_revision);

  std::set<revision_id>::const_iterator parent;
  for (parent = parents.begin(); parent != parents.end(); parent++) {
    if (*parent == null_revision) {
      // work_unit.node_revision is the root node
      L(F("do_annotate_node setting root revision as %s\n") % work_unit.node_revision);
      work_unit.annotations->set_root_revision(work_unit.node_revision);
      return;
    }

    change_set cs;
    calculate_arbitrary_change_set (*parent, work_unit.node_revision, app, cs);

    file_path parent_fpath = apply_change_set_inverse(cs, work_unit.node_fpath);
    L(F("file %s in parent revision %s is %s\n") % work_unit.node_fpath % *parent % parent_fpath);

    if (parent_fpath == std::string("")) {
      // I(work_unit.node_revision added work_unit.node_fid)
      L(F("revision %s added file %s (file id %s), terminating annotation processing\n") 
        % work_unit.node_revision % work_unit.node_fpath % work_unit.node_fid);
      work_unit.annotations->complete(work_unit.node_revision);
      return;
    }
    file_id parent_fid = find_file_id_in_revision(app, parent_fpath, *parent);

    boost::shared_ptr<annotate_lineage> parent_lineage;

    if (! (work_unit.node_fid == parent_fid)) {
      file_delta delta;
      app.db.get_file_delta(parent_fid, work_unit.node_fid, delta);
      parent_lineage = apply_delta_annotation(work_unit, delta);
    } else {
      parent_lineage = work_unit.lineage;
    }

    // if this parent has not yet been queued for processing, create the work unit for it.
    if (nodes_seen.find(*parent) == nodes_seen.end()) {
      nodes_seen.insert(*parent);
      annotate_node_work newunit(work_unit.annotations, parent_lineage, *parent, parent_fid, parent_fpath);
      nodes_to_process.push_back(newunit);
    }
  }

  work_unit.annotations->evaluate(work_unit.node_revision);
}


void
write_annotations (boost::shared_ptr<annotate_context> acp, 
                   boost::shared_ptr<annotation_formatter> frmt) 
{
}
