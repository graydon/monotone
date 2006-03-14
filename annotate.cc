// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2005 emile snyder <emile@alumni.reed.edu>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <sstream>
#include <deque>
#include <set>

#include <boost/shared_ptr.hpp>

#include "annotate.hh"
#include "app_state.hh"
#include "cset.hh"
#include "interner.hh"
#include "lcs.hh"
#include "platform.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"
#include "cert.hh"
#include "ui.hh"


class annotate_lineage_mapping;


class annotate_context {
public:
  annotate_context(file_id fid, app_state &app);

  boost::shared_ptr<annotate_lineage_mapping> initial_lineage() const;

  /// credit any uncopied lines (as recorded in copied_lines) to
  /// rev, and reset copied_lines.
  void evaluate(revision_id rev);

  void set_copied(int index);
  void set_touched(int index);

  void set_equivalent(int index, int index2);
  void annotate_equivalent_lines();

  /// return true if we have no more unassigned lines
  bool is_complete() const;

  void dump(app_state & app) const;

  std::string get_line(int line_index) const { return file_lines[line_index]; }

private:
  void build_revisions_to_annotations(app_state & app, std::map<revision_id, std::string> & revs_to_notations) const;

  std::vector<std::string> file_lines;
  std::vector<revision_id> annotations;

  /// equivalent_lines[n] = m means that line n should be blamed to the same 
  /// revision as line m
  std::map<int, int> equivalent_lines;

  /// keep a count so we can tell quickly whether we can terminate
  size_t annotated_lines_completed;

  // elements of the set are indexes into the array of lines in the UDOI
  // lineages add entries here when they notice that they copied a line from the UDOI
  std::set<size_t> copied_lines;

  // similarly, lineages add entries here for all the lines from the UDOI they know about that they didn't copy
  std::set<size_t> touched_lines;
};



/*
  An annotate_lineage_mapping tells you, for each line of a file, where in the 
  ultimate descendent of interest (UDOI) the line came from (a line not
  present in the UDOI is represented as -1).
*/
class annotate_lineage_mapping {
public:
  annotate_lineage_mapping(const file_data &data);
  annotate_lineage_mapping(const std::vector<std::string> &lines);

  // debugging
  //bool equal_interned (const annotate_lineage_mapping &rhs) const;

  /// need a better name.  does the work of setting copied bits in the context object.
  boost::shared_ptr<annotate_lineage_mapping> 
  build_parent_lineage(boost::shared_ptr<annotate_context> acp, revision_id parent_rev, const file_data &parent_data) const;

  void merge(const annotate_lineage_mapping &other, const boost::shared_ptr<annotate_context> &acp);

  void credit_mapped_lines (boost::shared_ptr<annotate_context> acp) const;
  void set_copied_all_mapped (boost::shared_ptr<annotate_context> acp) const;

private:
  void init_with_lines(const std::vector<std::string> &lines);

  static interner<long> in; // FIX, testing hack 

  std::vector<long, QA(long)> file_interned;

  // maps an index into the vector of lines for our current version of the file
  // into an index into the vector of lines of the UDOI:
  // eg. if the line file_interned[i] will turn into line 4 in the UDOI, mapping[i] = 4
  std::vector<int> mapping;
};


interner<long> annotate_lineage_mapping::in;


/*
  annotate_node_work encapsulates the input data needed to process 
  the annotations for a given childrev, considering all the
  childrev -> parentrevN edges.
*/
struct annotate_node_work {
  annotate_node_work (boost::shared_ptr<annotate_context> annotations_,
                      boost::shared_ptr<annotate_lineage_mapping> lineage_,
                      revision_id revision_, node_id fid_)//, file_path node_fpath_)
    : annotations(annotations_), 
      lineage(lineage_),
      revision(revision_), 
      fid(fid_)//, node_fpath(node_fpath_)
  {}
  annotate_node_work (const annotate_node_work &w)
    : annotations(w.annotations), 
      lineage(w.lineage),
      revision(w.revision),
      fid(w.fid) //, node_fpath(w.node_fpath)
  {}

  boost::shared_ptr<annotate_context> annotations;
  boost::shared_ptr<annotate_lineage_mapping> lineage;
  revision_id revision;
  //file_id     node_fid;
  node_id fid;
  //file_path   node_fpath;
};


class lineage_merge_node {
public:
  typedef boost::shared_ptr<annotate_lineage_mapping> splm;

  lineage_merge_node(const lineage_merge_node &m)
    : work(m.work), incoming_edges(m.incoming_edges), completed_edges(m.completed_edges)
  {}

  lineage_merge_node(annotate_node_work wu, size_t incoming)
    : work(wu), incoming_edges(incoming), completed_edges(1)
  {}
  
  void merge(splm incoming, const boost::shared_ptr<annotate_context> &acp)
  { 
    work.lineage->merge(*incoming, acp); completed_edges++;
  }

  bool iscomplete () const { I(completed_edges <= incoming_edges); return incoming_edges == completed_edges; }
  
  annotate_node_work get_work() const { I(iscomplete()); return work; }

private:
  annotate_node_work work;
  size_t incoming_edges;
  size_t completed_edges;
};





annotate_context::annotate_context(file_id fid, app_state &app)
  : annotated_lines_completed(0)
{
  // initialize file_lines
  file_data fpacked;
  app.db.get_file_version(fid, fpacked);
  std::string encoding = default_encoding; // FIXME
  split_into_lines(fpacked.inner()(), encoding, file_lines);
  L(FL("annotate_context::annotate_context initialized with %d file lines\n") % file_lines.size());

  // initialize annotations
  revision_id nullid;
  annotations.clear();
  annotations.reserve(file_lines.size());
  annotations.insert(annotations.begin(), file_lines.size(), nullid);
  L(FL("annotate_context::annotate_context initialized with %d entries in annotations\n") % annotations.size());

  // initialize copied_lines and touched_lines
  copied_lines.clear();
  touched_lines.clear();
}


boost::shared_ptr<annotate_lineage_mapping> 
annotate_context::initial_lineage() const
{
  boost::shared_ptr<annotate_lineage_mapping> res(new annotate_lineage_mapping(file_lines));
  return res;
}


void 
annotate_context::evaluate(revision_id rev)
{
  revision_id nullid;
  I(copied_lines.size() <= annotations.size());
  I(touched_lines.size() <= annotations.size());

  // Find the lines that we touched but that no other parent copied.
  std::set<size_t> credit_lines;
  std::set_difference(touched_lines.begin(), touched_lines.end(),
                      copied_lines.begin(), copied_lines.end(),
                      inserter(credit_lines, credit_lines.begin()));

  std::set<size_t>::const_iterator i;
  for (i = credit_lines.begin(); i != credit_lines.end(); i++) {
    I(*i < annotations.size());
    if (annotations[*i] == nullid) {
      //L(FL("evaluate setting annotations[%d] -> %s, since touched_lines contained %d, copied_lines didn't and annotations[%d] was nullid\n") 
      //  % *i % rev % *i % *i);
      annotations[*i] = rev;
      annotated_lines_completed++;
    } else {
      //L(FL("evaluate LEAVING annotations[%d] -> %s\n") % *i % annotations[*i]);
    }
  }

  copied_lines.clear();
  touched_lines.clear();
}

void 
annotate_context::set_copied(int index)
{
  //L(FL("annotate_context::set_copied %d\n") % index);
  if (index == -1)
    return;

  I(index >= 0 && index < (int)file_lines.size());
  copied_lines.insert(index);
}

void 
annotate_context::set_touched(int index)
{
  //L(FL("annotate_context::set_touched %d\n") % index);
  if (index == -1)
    return;

  I(index >= 0 && index <= (int)file_lines.size());
  touched_lines.insert(index);
}

void
annotate_context::set_equivalent(int index, int index2)
{
  L(FL("annotate_context::set_equivalent index %d index2 %d\n") % index % index2);
  equivalent_lines[index] = index2;
}

void
annotate_context::annotate_equivalent_lines()
{
  revision_id null_id;

  for (size_t i=0; i<annotations.size(); i++) {
    if (annotations[i] == null_id) {
      std::map<int, int>::const_iterator j = equivalent_lines.find(i);
      if (j == equivalent_lines.end()) {
        L(FL("annotate_equivalent_lines unable to find equivalent for line %d\n") % i);
      }
      I(j != equivalent_lines.end());
      annotations[i] = annotations[j->second];
      annotated_lines_completed++;
    }
  }
}

bool
annotate_context::is_complete() const
{
  if (annotated_lines_completed == annotations.size())
    return true;

  I(annotated_lines_completed < annotations.size());
  return false;
}


std::string cert_string_value (std::vector< revision<cert> > const & certs,
                               const std::string & name, 
                               bool from_start, bool from_end, 
                               const std::string & sep)
{
  for (std::vector < revision < cert > >::const_iterator i = certs.begin ();
       i != certs.end (); ++i)
    {
      if (i->inner ().name () == name)
        {
          cert_value tv;
          decode_base64 (i->inner ().value, tv);
          std::string::size_type f = 0;
          std::string::size_type l = std::string::npos;
          if (from_start)
            l = tv ().find_first_of (sep);
          if (from_end)
            {
              f = tv ().find_last_of (sep);
              if (f == std::string::npos)
                f = 0;
            }
          return tv ().substr (f, l);
        }
    }

  return "";
}


void
annotate_context::build_revisions_to_annotations(app_state &app,
                                                 std::map<revision_id, std::string> &revs_to_notations) const
{
  I(annotations.size() == file_lines.size());

  // build set of unique revisions present in annotations
  std::set<revision_id> seen;
  for (std::vector<revision_id>::const_iterator i = annotations.begin(); i != annotations.end(); i++)
    {
      seen.insert(*i);
    }

  size_t max_note_length = 0;

  // build revision -> annotation string mapping
  for (std::set<revision_id>::const_iterator i = seen.begin(); i != seen.end(); i++)
    {
      std::vector< revision<cert> > certs;
      app.db.get_revision_certs(*i, certs);
      erase_bogus_certs(certs, app);

      std::string author(cert_string_value(certs, author_cert_name, true, false, "@< "));
      std::string date(cert_string_value(certs, date_cert_name, true, false, "T"));

      std::string result;
      result.append((*i).inner ()().substr(0, 8));
      result.append(".. by ");
      result.append(author);
      result.append(" ");
      result.append(date);
      result.append(": ");

      max_note_length = (result.size() > max_note_length) ? result.size() : max_note_length;
      revs_to_notations[*i] = result;
    }

  // justify annotation strings
  for (std::map<revision_id, std::string>::iterator i = revs_to_notations.begin(); i != revs_to_notations.end(); i++)
    {
      size_t l = i->second.size();
      i->second.insert(std::string::size_type(0), max_note_length - l, ' ');
    }
}


void
annotate_context::dump(app_state &app) const
{
  revision_id nullid;
  I(annotations.size() == file_lines.size());

  std::map<revision_id, std::string> revs_to_notations;
  std::string empty_note;
  if (global_sanity.brief) 
    {
      build_revisions_to_annotations(app, revs_to_notations);
      size_t max_note_length = revs_to_notations.begin()->second.size();
      empty_note.insert(std::string::size_type(0), max_note_length - 2, ' ');
    }

  revision_id lastid = nullid;
  for (size_t i=0; i<file_lines.size(); i++) 
    {
      //I(! (annotations[i] == nullid) );
      if (global_sanity.brief)
        {
          if (lastid == annotations[i])
            std::cout << empty_note << ": " << file_lines[i] << std::endl;
          else
            std::cout << revs_to_notations[annotations[i]] << file_lines[i] << std::endl;
          lastid = annotations[i];
        } 
      else
        std::cout << annotations[i] << ": " << file_lines[i] << std::endl;
    }
}


annotate_lineage_mapping::annotate_lineage_mapping(const file_data &data)
{
  // split into lines
  std::vector<std::string> lines;
  std::string encoding = default_encoding; // FIXME
  split_into_lines (data.inner()().data(), encoding, lines);

  init_with_lines(lines);
}

annotate_lineage_mapping::annotate_lineage_mapping(const std::vector<std::string> &lines)
{
  init_with_lines(lines);
}

/*
bool
annotate_lineage_mapping::equal_interned (const annotate_lineage_mapping &rhs) const
{
  bool result = true;

  if (file_interned.size() != rhs.file_interned.size()) {
    L(FL("annotate_lineage_mapping::equal_interned lhs size %d != rhs size %d\n")
      % file_interned.size() % rhs.file_interned.size());
    result = false;
  }

  size_t limit = std::min(file_interned.size(), rhs.file_interned.size());
  for (size_t i=0; i<limit; i++) {
    if (file_interned[i] != rhs.file_interned[i]) {
      L(FL("annotate_lineage_mapping::equal_interned lhs[%d]:%ld != rhs[%d]:%ld\n")
        % i % file_interned[i] % i % rhs.file_interned[i]);
      result = false;
    }
  }

  return result;
}
*/

void
annotate_lineage_mapping::init_with_lines(const std::vector<std::string> &lines)
{
  file_interned.clear();
  file_interned.reserve(lines.size());
  mapping.clear();
  mapping.reserve(lines.size());

  int count;
  std::vector<std::string>::const_iterator i;
  for (count=0, i = lines.begin(); i != lines.end(); i++, count++) {
    file_interned.push_back(in.intern(*i));
    mapping.push_back(count);
  }
  L(FL("annotate_lineage_mapping::init_with_lines  ending with %d entries in mapping\n") % mapping.size());
}


boost::shared_ptr<annotate_lineage_mapping>
annotate_lineage_mapping::build_parent_lineage (boost::shared_ptr<annotate_context> acp, 
                                                revision_id parent_rev, 
                                                const file_data &parent_data) const
{
  bool verbose = false;
  boost::shared_ptr<annotate_lineage_mapping> parent_lineage(new annotate_lineage_mapping(parent_data));

  std::vector<long, QA(long)> lcs;
  std::back_insert_iterator< std::vector<long, QA(long)> > bii(lcs);
  longest_common_subsequence(file_interned.begin(), 
                             file_interned.end(),
                             parent_lineage->file_interned.begin(), 
                             parent_lineage->file_interned.end(),
                             std::min(file_interned.size(), parent_lineage->file_interned.size()),
                             std::back_inserter(lcs));

  if (verbose)
    L(FL("build_parent_lineage: file_lines.size() == %d, parent.file_lines.size() == %d, lcs.size() == %d\n")
      % file_interned.size() % parent_lineage->file_interned.size() % lcs.size());

  // do the copied lines thing for our annotate_context
  std::vector<long> lcs_src_lines;
  lcs_src_lines.resize(lcs.size());
  size_t i, j;
  i = j = 0;
  while (i < file_interned.size() && j < lcs.size()) {
    //if (verbose)
    if (file_interned[i] == 14)
      L(FL("%s file_interned[%d]: %ld\tlcs[%d]: %ld\tmapping[%d]: %ld\n") 
        % parent_rev % i % file_interned[i] % j % lcs[j] % i % mapping[i]);

    if (file_interned[i] == lcs[j]) {
      acp->set_copied(mapping[i]);
      lcs_src_lines[j] = mapping[i];
      j++;
    } else {
      acp->set_touched(mapping[i]);
    }

    i++;
  }
  if (verbose)
    L(FL("loop ended with i: %d, j: %d, lcs.size(): %d\n") % i % j % lcs.size());
  I(j == lcs.size());

  // set touched for the rest of the lines in the file
  while (i < file_interned.size()) {
    acp->set_touched(mapping[i]);
    i++;
  }

  // determine the mapping for parent lineage
  if (verbose)
    L(FL("build_parent_lineage: building mapping now for parent_rev %s\n") % parent_rev);
  i = j = 0;
  while (i < parent_lineage->file_interned.size() && j < lcs.size()) {
    if (parent_lineage->file_interned[i] == lcs[j]) {
      parent_lineage->mapping[i] = lcs_src_lines[j];
      j++;
    } else {
      parent_lineage->mapping[i] = -1;
    }
    if (verbose)
      L(FL("mapping[%d] -> %d\n") % i % parent_lineage->mapping[i]);
    
    i++;
  }
  I(j == lcs.size());
  // set mapping for the rest of the lines in the file
  while (i < parent_lineage->file_interned.size()) {
    parent_lineage->mapping[i] = -1;
    if (verbose)
      L(FL("mapping[%d] -> %d\n") % i % parent_lineage->mapping[i]);
    i++;
  }

  return parent_lineage;
}


void
annotate_lineage_mapping::merge (const annotate_lineage_mapping &other, 
                                 const boost::shared_ptr<annotate_context> &acp)
{
  I(file_interned.size() == other.file_interned.size());
  I(mapping.size() == other.mapping.size());
  //I(equal_interned(other)); // expensive check

  for (size_t i=0; i<mapping.size(); i++) {
    if (mapping[i] == -1 && other.mapping[i] >= 0)
      mapping[i] = other.mapping[i];

    if (mapping[i] >= 0 && other.mapping[i] >= 0) {
      //I(mapping[i] == other.mapping[i]);
      if (mapping[i] != other.mapping[i]) {
        // a given line in the current merged mapping will split and become 
        // multiple lines in the UDOI.  so we have to remember that whenever we
        // ultimately assign blame for mapping[i] we blame the same revision
        // on other.mapping[i].
        acp->set_equivalent(other.mapping[i], mapping[i]);
      }
    }
  }
}

void
annotate_lineage_mapping::credit_mapped_lines (boost::shared_ptr<annotate_context> acp) const
{
  std::vector<int>::const_iterator i;
  for (i=mapping.begin(); i != mapping.end(); i++) {
    acp->set_touched(*i);
  }
}


void 
annotate_lineage_mapping::set_copied_all_mapped (boost::shared_ptr<annotate_context> acp) const
{
  std::vector<int>::const_iterator i;
  for (i=mapping.begin(); i != mapping.end(); i++) {
    acp->set_copied(*i);
  }
}


static void
do_annotate_node (const annotate_node_work &work_unit, 
                  app_state &app,
                  std::deque<annotate_node_work> &nodes_to_process,
                  std::set<revision_id> &nodes_complete,
                  const std::map<revision_id, size_t> &paths_to_nodes,
                  std::map<revision_id, lineage_merge_node> &pending_merge_nodes)
{
  L(FL("do_annotate_node for node %s\n") % work_unit.revision);
  I(nodes_complete.find(work_unit.revision) == nodes_complete.end());
  // nodes_seen.insert(std::make_pair(work_unit.revision, work_unit.lineage));

  roster_t roster;
  marking_map markmap;
  app.db.get_roster(work_unit.revision, roster, markmap);
  marking_t marks;
  std::map<node_id, marking_t>::const_iterator mmi = markmap.find(work_unit.fid);
  I(mmi != markmap.end());
  marks = mmi->second;

  if (marks.file_content.size() == 0)
    {
      L(FL("found empty content-mark set at rev %s\n") % work_unit.revision);
      work_unit.lineage->credit_mapped_lines(work_unit.annotations);
      work_unit.annotations->evaluate(work_unit.revision);
      nodes_complete.insert(work_unit.revision);
      return;
    }

  std::set<revision_id> parents;

  // If we have content-marks which are *not* equal to the current rev,
  // we can jump back to them directly. If we have only a content-mark
  // equal to the current rev, it means we made a decision here, and 
  // we must move to the immediate parent revs.
  //
  // Unfortunately, while this algorithm *could* use the marking
  // information as suggested above, it seems to work much better
  // (especially wrt. merges) when it goes rev-by-rev, so we leave it that
  // way for now.
  
  //   if (marks.file_content.size() == 1 
  //       && *(marks.file_content.begin()) == work_unit.revision)
  app.db.get_revision_parents(work_unit.revision, parents);
  //   else
  //     parents = marks.file_content;

  size_t added_in_parent_count = 0;

  for (std::set<revision_id>::const_iterator i = parents.begin(); 
       i != parents.end(); i++)
    {
      revision_id parent_revision = *i;

      roster_t parent_roster;
      marking_map parent_marks;
      L(FL("do_annotate_node processing edge from parent %s to child %s\n") 
        % parent_revision % work_unit.revision);

      I(!(work_unit.revision == parent_revision));
      app.db.get_roster(parent_revision, parent_roster, parent_marks);

      if (!parent_roster.has_node(work_unit.fid))
        {
          L(FL("file added in %s, continuing\n") % work_unit.revision);
          added_in_parent_count++;
          continue;
        }

      // The node was live in the parent, so this represents a delta.
      file_t file_in_child = downcast_to_file_t(roster.get_node(work_unit.fid));
      file_t file_in_parent = downcast_to_file_t(parent_roster.get_node(work_unit.fid));

      boost::shared_ptr<annotate_lineage_mapping> parent_lineage;

      if (file_in_parent->content == file_in_child->content)
        {
          L(FL("parent file identical, set copied all mapped and copy lineage\n"));
          parent_lineage = work_unit.lineage;
          parent_lineage->set_copied_all_mapped(work_unit.annotations);
        }
      else
        {
          file_data data;
          app.db.get_file_version(file_in_parent->content, data);      
          L(FL("building parent lineage for parent file %s\n") % file_in_parent->content);          
          parent_lineage = work_unit.lineage->build_parent_lineage(work_unit.annotations,
                                                                   parent_revision,
                                                                   data);
        }

      // If this parent has not yet been queued for processing, create the
      // work unit for it.
      std::map<revision_id, lineage_merge_node>::iterator lmn 
        = pending_merge_nodes.find(parent_revision);

      if (lmn == pending_merge_nodes.end()) 
        {
          // Once we move on to processing the parent that this file was
          // renamed from, we'll need the old name
          annotate_node_work newunit(work_unit.annotations, 
                                     parent_lineage, 
                                     parent_revision, 
                                     work_unit.fid);

          std::map<revision_id, size_t>::const_iterator ptn 
            = paths_to_nodes.find(parent_revision);

          if (ptn->second > 1) 
            {
              lineage_merge_node nmn(newunit, ptn->second);
              pending_merge_nodes.insert(std::make_pair(parent_revision, nmn));
              L(FL("put new merge node on pending_merge_nodes for parent %s\n") 
                % parent_revision);
              // just checking...
              //(pending_merge_nodes.find(parent_revision))->second.dump();
            }
          else 
            {
              L(FL("single path to node, just stick work on the queue for parent %s\n") 
                % parent_revision);
              nodes_to_process.push_back(newunit);
            }
        } 
      else 
        {
          // Already a pending node, so we just have to merge the lineage
          // and decide whether to move it over to the nodes_to_process
          // queue.
          L(FL("merging lineage from node %s to parent %s\n") 
            % work_unit.revision % parent_revision);
          lmn->second.merge(parent_lineage, work_unit.annotations);
          //L(FL("after merging from work revision %s to parent %s"
          // " lineage_merge_node is:\n") % work_unit.revision 
          // % parent_revision); lmn->second.dump();
          if (lmn->second.iscomplete()) {
            nodes_to_process.push_back(lmn->second.get_work());
            pending_merge_nodes.erase(lmn);
          }
        }
    }

  if (added_in_parent_count == parents.size()) 
    {
      work_unit.lineage->credit_mapped_lines(work_unit.annotations);
    }
  
  work_unit.annotations->evaluate(work_unit.revision);
  nodes_complete.insert(work_unit.revision);
}


void 
find_ancestors(app_state &app, revision_id rid, std::map<revision_id, size_t> &paths_to_nodes)
{
  std::vector<revision_id> frontier;
  frontier.push_back(rid);

  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      if(!null_id(rid)) {
        std::set<revision_id> parents;
        app.db.get_revision_parents(rid, parents);
        for (std::set<revision_id>::const_iterator i = parents.begin();
             i != parents.end(); ++i)
          {
            std::map<revision_id, size_t>::iterator found = paths_to_nodes.find(*i);
            if (found == paths_to_nodes.end())
              {
                frontier.push_back(*i);
                paths_to_nodes.insert(std::make_pair(*i, 1));
              }
            else
              {
                (found->second)++;
              }
          }
      }
    }
}

void 
do_annotate (app_state &app, file_t file_node, revision_id rid)
{
  L(FL("annotating file %s with content %s in revision %s\n") % file_node->self % file_node->content % rid);

  boost::shared_ptr<annotate_context> acp(new annotate_context(file_node->content, app));
  boost::shared_ptr<annotate_lineage_mapping> lineage = acp->initial_lineage();

  std::set<revision_id> nodes_complete;
  std::map<revision_id, size_t> paths_to_nodes;
  std::map<revision_id, lineage_merge_node> pending_merge_nodes;
  find_ancestors(app, rid, paths_to_nodes);

  // build node work unit
  std::deque<annotate_node_work> nodes_to_process;
  annotate_node_work workunit(acp, lineage, rid, file_node->self); //, fpath);
  nodes_to_process.push_back(workunit);

  std::auto_ptr<ticker> revs_ticker(new ticker(_("revs done"), "r", 1));
  revs_ticker->set_total(paths_to_nodes.size() + 1);
  while (nodes_to_process.size() && !acp->is_complete()) 
    {
      annotate_node_work work = nodes_to_process.front();
      nodes_to_process.pop_front();
      do_annotate_node(work, app, nodes_to_process, nodes_complete, paths_to_nodes, pending_merge_nodes);
      ++(*revs_ticker);
    }
  
  I(pending_merge_nodes.size() == 0);
  acp->annotate_equivalent_lines();
  I(acp->is_complete());

  acp->dump(app);
}
