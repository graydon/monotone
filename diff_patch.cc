// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"
#include <algorithm>
#include <iterator>
#include <map>
#include "vector.hh"

#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include "diff_patch.hh"
#include "interner.hh"
#include "lcs.hh"
#include "roster.hh"
#include "safe_map.hh"
#include "sanity.hh"
#include "xdelta.hh"
#include "simplestring_xform.hh"
#include "vocab.hh"
#include "revision.hh"
#include "constants.hh"
#include "file_io.hh"
#include "pcrewrap.hh"
#include "lua_hooks.hh"
#include "database.hh"
#include "transforms.hh"

using std::make_pair;
using std::map;
using std::min;
using std::max;
using std::ostream;
using std::ostream_iterator;
using std::string;
using std::swap;
using std::vector;

using boost::shared_ptr;

//
// a 3-way merge works like this:
//
//            /---->   right
//    ancestor
//            \---->   left
//
// first you compute the edit list "EDITS(ancestor,left)".
//
// then you make an offset table "leftpos" which describes positions in
// "ancestor" as they map to "left"; that is, for 0 < apos <
// ancestor.size(), we have
//
//  left[leftpos[apos]] == ancestor[apos]
//
// you do this by walking through the edit list and either jumping the
// current index ahead an extra position, on an insert, or remaining still,
// on a delete. on an insert *or* a delete, you push the current index back
// onto the leftpos array.
//
// next you compute the edit list "EDITS(ancestor,right)".
//
// you then go through this edit list applying the edits to left, rather
// than ancestor, and using the table leftpos to map the position of each
// edit to an appropriate spot in left. this means you walk a "curr_left"
// index through the edits, and for each edit e:
//
// - if e is a delete (and e.pos is a position in ancestor)
//   - increment curr_left without copying anything to "merged"
//
// - if e is an insert (and e.pos is a position in right)
//   - copy right[e.pos] to "merged"
//   - leave curr_left alone
//
// - when advancing to apos (and apos is a position in ancestor)
//   - copy left[curr_left] to merged while curr_left < leftpos[apos]
//
//
// the practical upshot is that you apply the delta from ancestor->right
// to the adjusted contexts in left, producing something vaguely like
// the concatenation of delta(ancestor,left) :: delta(ancestor,right).
//
// NB: this is, as far as I can tell, what diff3 does. I don't think I'm
// infringing on anyone's fancy patents here.
//

typedef enum { preserved = 0, deleted = 1, changed = 2 } edit_t;
static const char etab[3][10] =
  {
    "preserved",
    "deleted",
    "changed"
  };

struct extent
{
  extent(size_t p, size_t l, edit_t t)
    : pos(p), len(l), type(t)
  {}
  size_t pos;
  size_t len;
  edit_t type;
};

void calculate_extents(vector<long, QA(long)> const & a_b_edits,
                       vector<long, QA(long)> const & b,
                       vector<long, QA(long)> & prefix,
                       vector<extent> & extents,
                       vector<long, QA(long)> & suffix,
                       size_t const a_len,
                       interner<long> & intern)
{
  extents.reserve(a_len * 2);

  size_t a_pos = 0, b_pos = 0;

  for (vector<long, QA(long)>::const_iterator i = a_b_edits.begin();
       i != a_b_edits.end(); ++i)
    {
      // L(FL("edit: %d") % *i);
      if (*i < 0)
        {
          // negative elements code the negation of the one-based index into A
          // of the element to be deleted
          size_t a_deleted = (-1 - *i);

          // fill positions out to the deletion point
          while (a_pos < a_deleted)
            {
              a_pos++;
              extents.push_back(extent(b_pos++, 1, preserved));
            }

          // L(FL(" -- delete at A-pos %d (B-pos = %d)") % a_deleted % b_pos);

          // skip the deleted line
          a_pos++;
          extents.push_back(extent(b_pos, 0, deleted));
        }
      else
        {
          // positive elements code the one-based index into B of the element to
          // be inserted
          size_t b_inserted = (*i - 1);

          // fill positions out to the insertion point
          while (b_pos < b_inserted)
            {
              a_pos++;
              extents.push_back(extent(b_pos++, 1, preserved));
            }

          // L(FL(" -- insert at B-pos %d (A-pos = %d) : '%s'")
          //   % b_inserted % a_pos % intern.lookup(b.at(b_inserted)));

          // record that there was an insertion, but a_pos did not move.
          if ((b_pos == 0 && extents.empty())
              || (b_pos == prefix.size()))
            {
              prefix.push_back(b.at(b_pos));
            }
          else if (a_len == a_pos)
            {
              suffix.push_back(b.at(b_pos));
            }
          else
            {
              // make the insertion
              extents.back().type = changed;
              extents.back().len++;
            }
          b_pos++;
        }
    }

  while (extents.size() < a_len)
    extents.push_back(extent(b_pos++, 1, preserved));
}

void normalize_extents(vector<extent> & a_b_map,
                       vector<long, QA(long)> const & a,
                       vector<long, QA(long)> const & b)
{
  for (size_t i = 0; i < a_b_map.size(); ++i)
    {
      if (i > 0)
      {
        size_t j = i;
        while (j > 0
               && (a_b_map.at(j-1).type == preserved)
               && (a_b_map.at(j).type == changed)
               && (a.at(j) == b.at(a_b_map.at(j).pos + a_b_map.at(j).len - 1)))
          {
            // This is implied by (a_b_map.at(j-1).type == preserved)
            I(a.at(j-1) == b.at(a_b_map.at(j-1).pos));

            // Coming into loop we have:
            //                     i
            //  z   --pres-->  z   0
            //  o   --pres-->  o   1
            //  a   --chng-->  a   2   The important thing here is that 'a' in
            //                 t       the LHS matches with ...
            //                 u
            //                 v
            //                 a       ... the a on the RHS here. Hence we can
            //  q  --pres-->   q   3   'shift' the entire 'changed' block
            //  e  --chng-->   d   4   upwards, leaving a 'preserved' line
            //  g  --pres-->   g   5   'a'->'a'
            //
            //  Want to end up with:
            //                     i
            //  z   --pres-->  z   0
            //  o   --chng-->  o   1
            //                 a
            //                 t
            //                 u
            //                 v
            //  a  --pres-->   a   2
            //  q  --pres-->   q   3
            //  e  --chng-->   d   4
            //  g  --pres-->   g   5
            //
            // Now all the 'changed' extents are normalised to the
            // earliest possible position.

            L(FL("exchanging preserved extent [%d+%d] with changed extent [%d+%d]")
              % a_b_map.at(j-1).pos
              % a_b_map.at(j-1).len
              % a_b_map.at(j).pos
              % a_b_map.at(j).len);

            swap(a_b_map.at(j-1).len, a_b_map.at(j).len);
            swap(a_b_map.at(j-1).type, a_b_map.at(j).type);

            // Adjust position of the later, preserved extent. It should
            // better point to the second 'a' in the above example.
            a_b_map.at(j).pos = a_b_map.at(j-1).pos + a_b_map.at(j-1).len;

            --j;
          }
      }
    }

  for (size_t i = 0; i < a_b_map.size(); ++i)
    {
      if (i > 0)
      {
        size_t j = i;
        while (j > 0
               && a_b_map.at(j).type == changed
               && a_b_map.at(j-1).type == changed
               && a_b_map.at(j).len > 1
               && a_b_map.at(j-1).pos + a_b_map.at(j-1).len == a_b_map.at(j).pos)
          {
            // step 1: move a chunk from this insert extent to its
            // predecessor
            size_t piece = a_b_map.at(j).len - 1;
            //      L(FL("moving change piece of len %d from pos %d to pos %d")
            //        % piece
            //        % a_b_map.at(j).pos
            //        % a_b_map.at(j-1).pos);
            a_b_map.at(j).len = 1;
            a_b_map.at(j).pos += piece;
            a_b_map.at(j-1).len += piece;

            // step 2: if this extent (now of length 1) has become a "changed"
            // extent identical to its previous state, switch it to a "preserved"
            // extent.
            if (b.at(a_b_map.at(j).pos) == a.at(j))
              {
                //              L(FL("changing normalized 'changed' extent at %d to 'preserved'")
                //                % a_b_map.at(j).pos);
                a_b_map.at(j).type = preserved;
              }
            --j;
          }
      }
    }
}


void merge_extents(vector<extent> const & a_b_map,
                   vector<extent> const & a_c_map,
                   vector<long, QA(long)> const & b,
                   vector<long, QA(long)> const & c,
                   interner<long> const & in,
                   vector<long, QA(long)> & merged)
{
  I(a_b_map.size() == a_c_map.size());

  vector<extent>::const_iterator i = a_b_map.begin();
  vector<extent>::const_iterator j = a_c_map.begin();
  merged.reserve(a_b_map.size() * 2);

  //   for (; i != a_b_map.end(); ++i, ++j)
  //     {

  //       L(FL("trying to merge: [%s %d %d] vs. [%s %d %d]")
  //            % etab[i->type] % i->pos % i->len
  //            % etab[j->type] % j->pos % j->len);
  //     }

  //   i = a_b_map.begin();
  //   j = a_c_map.begin();

  for (; i != a_b_map.end(); ++i, ++j)
    {

      //       L(FL("trying to merge: [%s %d %d] vs. [%s %d %d]")
      //                % etab[i->type] % i->pos % i->len
      //                % etab[j->type] % j->pos % j->len);

      // mutual, identical preserves / inserts / changes
      if (((i->type == changed && j->type == changed)
           || (i->type == preserved && j->type == preserved))
          && i->len == j->len)
        {
          for (size_t k = 0; k < i->len; ++k)
            {
              if (b.at(i->pos + k) != c.at(j->pos + k))
                {
                  L(FL("conflicting edits: %s %d[%d] '%s' vs. %s %d[%d] '%s'")
                    % etab[i->type] % i->pos % k % in.lookup(b.at(i->pos + k))
                    % etab[j->type] % j->pos % k % in.lookup(c.at(j->pos + k)));
                  throw conflict();
                }
              merged.push_back(b.at(i->pos + k));
            }
        }

      // mutual or single-edge deletes
      else if ((i->type == deleted && j->type == deleted)
               || (i->type == deleted && j->type == preserved)
               || (i->type == preserved && j->type == deleted))
        {
          // do nothing
        }

      // single-edge insert / changes
      else if (i->type == changed && j->type == preserved)
        for (size_t k = 0; k < i->len; ++k)
          merged.push_back(b.at(i->pos + k));

      else if (i->type == preserved && j->type == changed)
        for (size_t k = 0; k < j->len; ++k)
          merged.push_back(c.at(j->pos + k));

      else
        {
          L(FL("conflicting edits: [%s %d %d] vs. [%s %d %d]")
            % etab[i->type] % i->pos % i->len
            % etab[j->type] % j->pos % j->len);
          throw conflict();
        }

      //       if (merged.empty())
      //        L(FL(" --> EMPTY"));
      //       else
      //                L(FL(" --> [%d]: %s") % (merged.size() - 1) % in.lookup(merged.back()));
    }
}


void merge_via_edit_scripts(vector<string> const & ancestor,
                            vector<string> const & left,
                            vector<string> const & right,
                            vector<string> & merged)
{
  vector<long, QA(long)> anc_interned;
  vector<long, QA(long)> left_interned, right_interned;
  vector<long, QA(long)> left_edits, right_edits;
  vector<long, QA(long)> left_prefix, right_prefix;
  vector<long, QA(long)> left_suffix, right_suffix;
  vector<extent> left_extents, right_extents;
  vector<long, QA(long)> merged_interned;
  interner<long> in;

  //   for (int i = 0; i < min(min(left.size(), right.size()), ancestor.size()); ++i)
  //     {
  //       cerr << '[' << i << "] " << left[i] << ' ' << ancestor[i] <<  ' ' << right[i] << '\n';
  //     }

  anc_interned.reserve(ancestor.size());
  for (vector<string>::const_iterator i = ancestor.begin();
       i != ancestor.end(); ++i)
    anc_interned.push_back(in.intern(*i));

  left_interned.reserve(left.size());
  for (vector<string>::const_iterator i = left.begin();
       i != left.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(right.size());
  for (vector<string>::const_iterator i = right.begin();
       i != right.end(); ++i)
    right_interned.push_back(in.intern(*i));

  L(FL("calculating left edit script on %d -> %d lines")
    % anc_interned.size() % left_interned.size());

  edit_script(anc_interned.begin(), anc_interned.end(),
              left_interned.begin(), left_interned.end(),
              min(ancestor.size(), left.size()),
              left_edits);

  L(FL("calculating right edit script on %d -> %d lines")
    % anc_interned.size() % right_interned.size());

  edit_script(anc_interned.begin(), anc_interned.end(),
              right_interned.begin(), right_interned.end(),
              min(ancestor.size(), right.size()),
              right_edits);

  L(FL("calculating left extents on %d edits") % left_edits.size());
  calculate_extents(left_edits, left_interned,
                    left_prefix, left_extents, left_suffix,
                    anc_interned.size(), in);

  L(FL("calculating right extents on %d edits") % right_edits.size());
  calculate_extents(right_edits, right_interned,
                    right_prefix, right_extents, right_suffix,
                    anc_interned.size(), in);

  L(FL("normalizing %d right extents") % right_extents.size());
  normalize_extents(right_extents, anc_interned, right_interned);

  L(FL("normalizing %d left extents") % left_extents.size());
  normalize_extents(left_extents, anc_interned, left_interned);


  if ((!right_prefix.empty()) && (!left_prefix.empty()))
    {
      L(FL("conflicting prefixes"));
      throw conflict();
    }

  if ((!right_suffix.empty()) && (!left_suffix.empty()))
    {
      L(FL("conflicting suffixes"));
      throw conflict();
    }

  L(FL("merging %d left, %d right extents")
    % left_extents.size() % right_extents.size());

  copy(left_prefix.begin(), left_prefix.end(), back_inserter(merged_interned));
  copy(right_prefix.begin(), right_prefix.end(), back_inserter(merged_interned));

  merge_extents(left_extents, right_extents,
                left_interned, right_interned,
                in, merged_interned);

  copy(left_suffix.begin(), left_suffix.end(), back_inserter(merged_interned));
  copy(right_suffix.begin(), right_suffix.end(), back_inserter(merged_interned));

  merged.reserve(merged_interned.size());
  for (vector<long, QA(long)>::const_iterator i = merged_interned.begin();
       i != merged_interned.end(); ++i)
    merged.push_back(in.lookup(*i));
}


bool merge3(vector<string> const & ancestor,
            vector<string> const & left,
            vector<string> const & right,
            vector<string> & merged)
{
  try
   {
      merge_via_edit_scripts(ancestor, left, right, merged);
    }
  catch(conflict &)
    {
      L(FL("conflict detected. no merge."));
      return false;
    }
  return true;
}


///////////////////////////////////////////////////////////////////////////
// content_merge_database_adaptor
///////////////////////////////////////////////////////////////////////////


content_merge_database_adaptor::content_merge_database_adaptor(database & db,
                                                               revision_id const & left,
                                                               revision_id const & right,
                                                               marking_map const & left_mm,
                                                               marking_map const & right_mm)
  : db(db), left_rid (left), right_rid (right), left_mm(left_mm), right_mm(right_mm)
{
  // FIXME: possibly refactor to run this lazily, as we don't
  // need to find common ancestors if we're never actually
  // called on to do content merging.
  find_common_ancestor_for_merge(db, left, right, lca);
}

void
content_merge_database_adaptor::record_merge(file_id const & left_ident,
                                             file_id const & right_ident,
                                             file_id const & merged_ident,
                                             file_data const & left_data,
                                             file_data const & right_data,
                                             file_data const & merged_data)
{
  L(FL("recording successful merge of %s <-> %s into %s")
    % left_ident
    % right_ident
    % merged_ident);

  transaction_guard guard(db);

  if (!(left_ident == merged_ident))
    {
      delta left_delta;
      diff(left_data.inner(), merged_data.inner(), left_delta);
      db.put_file_version(left_ident, merged_ident, file_delta(left_delta));
    }
  if (!(right_ident == merged_ident))
    {
      delta right_delta;
      diff(right_data.inner(), merged_data.inner(), right_delta);
      db.put_file_version(right_ident, merged_ident, file_delta(right_delta));
    }
  guard.commit();
}

void
content_merge_database_adaptor::cache_roster(revision_id const & rid,
                                             boost::shared_ptr<roster_t const> roster)
{
  safe_insert(rosters, make_pair(rid, roster));
};

static void
load_and_cache_roster(database & db, revision_id const & rid,
                      map<revision_id, shared_ptr<roster_t const> > & rmap,
                      shared_ptr<roster_t const> & rout)
{
  map<revision_id, shared_ptr<roster_t const> >::const_iterator i = rmap.find(rid);
  if (i != rmap.end())
    rout = i->second;
  else
    {
      cached_roster cr;
      db.get_roster(rid, cr);
      safe_insert(rmap, make_pair(rid, cr.first));
      rout = cr.first;
    }
}

void
content_merge_database_adaptor::get_ancestral_roster(node_id nid,
                                                     revision_id & rid,
                                                     shared_ptr<roster_t const> & anc)
{
  // Given a file, if the lca is nonzero and its roster contains the file,
  // then we use its roster.  Otherwise we use the roster at the file's
  // birth revision, which is the "per-file worst case" lca.

  // Begin by loading any non-empty file lca roster
  rid = lca;
  if (!lca.inner()().empty())
    load_and_cache_roster(db, lca, rosters, anc);

  // If there is no LCA, or the LCA's roster doesn't contain the file,
  // then use the file's birth roster.
  if (!anc || !anc->has_node(nid))
    {
      marking_map::const_iterator lmm = left_mm.find(nid);
      marking_map::const_iterator rmm = right_mm.find(nid);

      MM(left_mm);
      MM(right_mm);

      if (lmm == left_mm.end())
        {
          I(rmm != right_mm.end());
          rid = rmm->second.birth_revision;
        }
      else if (rmm == right_mm.end())
        {
          I(lmm != left_mm.end());
          rid = lmm->second.birth_revision;
        }
      else
        {
          I(lmm->second.birth_revision == rmm->second.birth_revision);
          rid = lmm->second.birth_revision;
        }

      load_and_cache_roster(db, rid, rosters, anc);
    }
  I(anc);
}

void
content_merge_database_adaptor::get_version(file_id const & ident,
                                            file_data & dat) const
{
  db.get_file_version(ident, dat);
}


///////////////////////////////////////////////////////////////////////////
// content_merge_workspace_adaptor
///////////////////////////////////////////////////////////////////////////

void
content_merge_workspace_adaptor::cache_roster(revision_id const & rid,
                                              boost::shared_ptr<roster_t const> roster)
{
  rosters.insert(std::make_pair(rid, roster));
}

void
content_merge_workspace_adaptor::record_merge(file_id const & left_id,
                                              file_id const & right_id,
                                              file_id const & merged_id,
                                              file_data const & left_data,
                                              file_data const & right_data,
                                              file_data const & merged_data)
{
  L(FL("temporarily recording merge of %s <-> %s into %s")
    % left_id
    % right_id
    % merged_id);
  // this is an insert instead of a safe_insert because it is perfectly
  // legal (though rare) to have multiple merges resolve to the same file
  // contents.
  temporary_store.insert(make_pair(merged_id, merged_data));
}

void
content_merge_workspace_adaptor::get_ancestral_roster(node_id nid,
                                                      revision_id & rid,
                                                      shared_ptr<roster_t const> & anc)
{
  // Begin by loading any non-empty file lca roster
  if (base->has_node(nid))
    {
      rid = lca;
      anc = base;
    }
  else
    {
      marking_map::const_iterator lmm = left_mm.find(nid);
      marking_map::const_iterator rmm = right_mm.find(nid);

      MM(left_mm);
      MM(right_mm);

      if (lmm == left_mm.end())
        {
          I(rmm != right_mm.end());
          rid = rmm->second.birth_revision;
        }
      else if (rmm == right_mm.end())
        {
          I(lmm != left_mm.end());
          rid = lmm->second.birth_revision;
        }
      else
        {
          I(lmm->second.birth_revision == rmm->second.birth_revision);
          rid = lmm->second.birth_revision;
        }

      load_and_cache_roster(db, rid, rosters, anc);
    }
  I(anc);
}

void
content_merge_workspace_adaptor::get_version(file_id const & ident,
                                             file_data & dat) const
{
  map<file_id,file_data>::const_iterator i = temporary_store.find(ident);
  if (i != temporary_store.end())
    dat = i->second;
  else if (db.file_version_exists(ident))
    db.get_file_version(ident, dat);
  else
    {
      data tmp;
      file_id fid;
      map<file_id, file_path>::const_iterator i = content_paths.find(ident);
      I(i != content_paths.end());

      require_path_is_file(i->second,
                           F("file '%s' does not exist in workspace") % i->second,
                           F("'%s' in workspace is a directory, not a file") % i->second);
      read_data(i->second, tmp);
      calculate_ident(file_data(tmp), fid);
      E(fid == ident,
        F("file %s in workspace has id %s, wanted %s")
        % i->second
        % fid
        % ident);
      dat = file_data(tmp);
    }
}


///////////////////////////////////////////////////////////////////////////
// content_merge_checkout_adaptor
///////////////////////////////////////////////////////////////////////////

void
content_merge_checkout_adaptor::record_merge(file_id const & left_ident,
                                             file_id const & right_ident,
                                             file_id const & merged_ident,
                                             file_data const & left_data,
                                             file_data const & right_data,
                                             file_data const & merged_data)
{
  I(false);
}

void
content_merge_checkout_adaptor::get_ancestral_roster(node_id nid,
                                                     revision_id & rid,
                                                     shared_ptr<roster_t const> & anc)
{
  I(false);
}

void
content_merge_checkout_adaptor::get_version(file_id const & ident,
                                            file_data & dat) const
{
  db.get_file_version(ident, dat);
}


///////////////////////////////////////////////////////////////////////////
// content_merger
///////////////////////////////////////////////////////////////////////////

string
content_merger::get_file_encoding(file_path const & path,
                                  roster_t const & ros)
{
  attr_value v;
  if (ros.get_attr(path, attr_key(constants::encoding_attribute), v))
    return v();
  return constants::default_encoding;
}

bool
content_merger::attribute_manual_merge(file_path const & path,
                                       roster_t const & ros)
{
  attr_value v;
  if (ros.get_attr(path, attr_key(constants::manual_merge_attribute), v)
      && v() == "true")
    return true;
  return false; // default: enable auto merge
}

bool
content_merger::try_auto_merge(file_path const & anc_path,
                               file_path const & left_path,
                               file_path const & right_path,
                               file_path const & merged_path,
                               file_id const & ancestor_id,
                               file_id const & left_id,
                               file_id const & right_id,
                               file_id & merged_id)
{
  // This version of try_to_merge_files should only be called when there is a
  // real merge3 to perform.
  I(!null_id(ancestor_id));
  I(!null_id(left_id));
  I(!null_id(right_id));

  L(FL("trying auto merge '%s' %s <-> %s (ancestor: %s)")
    % merged_path
    % left_id
    % right_id
    % ancestor_id);

  if (left_id == right_id)
    {
      L(FL("files are identical"));
      merged_id = left_id;
      return true;
    }

  file_data left_data, right_data, ancestor_data;
  data left_unpacked, ancestor_unpacked, right_unpacked, merged_unpacked;

  adaptor.get_version(left_id, left_data);
  adaptor.get_version(ancestor_id, ancestor_data);
  adaptor.get_version(right_id, right_data);

  left_unpacked = left_data.inner();
  ancestor_unpacked = ancestor_data.inner();
  right_unpacked = right_data.inner();

  if (!attribute_manual_merge(left_path, left_ros) &&
      !attribute_manual_merge(right_path, right_ros))
    {
      // both files mergeable by monotone internal algorithm, try to merge
      // note: the ancestor is not considered for manual merging. Forcing the
      // user to merge manually just because of an ancestor mistakenly marked
      // manual seems too harsh
      string left_encoding, anc_encoding, right_encoding;
      left_encoding = this->get_file_encoding(left_path, left_ros);
      anc_encoding = this->get_file_encoding(anc_path, anc_ros);
      right_encoding = this->get_file_encoding(right_path, right_ros);

      vector<string> left_lines, ancestor_lines, right_lines, merged_lines;
      split_into_lines(left_unpacked(), left_encoding, left_lines);
      split_into_lines(ancestor_unpacked(), anc_encoding, ancestor_lines);
      split_into_lines(right_unpacked(), right_encoding, right_lines);

      if (merge3(ancestor_lines, left_lines, right_lines, merged_lines))
        {
          file_id tmp_id;
          file_data merge_data;
          string tmp;

          L(FL("internal 3-way merged ok"));
          join_lines(merged_lines, tmp);
          merge_data = file_data(tmp);
          calculate_ident(merge_data, merged_id);

          adaptor.record_merge(left_id, right_id, merged_id,
                               left_data, right_data, merge_data);

          return true;
        }
    }

  return false;
}

bool
content_merger::try_user_merge(file_path const & anc_path,
                               file_path const & left_path,
                               file_path const & right_path,
                               file_path const & merged_path,
                               file_id const & ancestor_id,
                               file_id const & left_id,
                               file_id const & right_id,
                               file_id & merged_id)
{
  // This version of try_to_merge_files should only be called when there is a
  // real merge3 to perform.
  I(!null_id(ancestor_id));
  I(!null_id(left_id));
  I(!null_id(right_id));

  L(FL("trying user merge '%s' %s <-> %s (ancestor: %s)")
    % merged_path
    % left_id
    % right_id
    % ancestor_id);

  if (left_id == right_id)
    {
      L(FL("files are identical"));
      merged_id = left_id;
      return true;
    }

  file_data left_data, right_data, ancestor_data;
  data left_unpacked, ancestor_unpacked, right_unpacked, merged_unpacked;

  adaptor.get_version(left_id, left_data);
  adaptor.get_version(ancestor_id, ancestor_data);
  adaptor.get_version(right_id, right_data);

  left_unpacked = left_data.inner();
  ancestor_unpacked = ancestor_data.inner();
  right_unpacked = right_data.inner();

  P(F("help required for 3-way merge\n"
      "[ancestor] %s\n"
      "[    left] %s\n"
      "[   right] %s\n"
      "[  merged] %s")
    % anc_path
    % left_path
    % right_path
    % merged_path);

  if (lua.hook_merge3(anc_path, left_path, right_path, merged_path,
                      ancestor_unpacked, left_unpacked,
                      right_unpacked, merged_unpacked))
    {
      file_data merge_data(merged_unpacked);

      L(FL("lua merge3 hook merged ok"));
      calculate_ident(merge_data, merged_id);

      adaptor.record_merge(left_id, right_id, merged_id,
                           left_data, right_data, merge_data);
      return true;
    }

  return false;
}

// the remaining part of this file just handles printing out various
// diff formats for the case where someone wants to *read* a diff
// rather than apply it.

struct hunk_consumer
{
  vector<string> const & a;
  vector<string> const & b;
  size_t ctx;
  ostream & ost;
  boost::scoped_ptr<pcre::regex const> encloser_re;
  size_t a_begin, b_begin, a_len, b_len;
  long skew;

  vector<string>::const_reverse_iterator encloser_last_match;
  vector<string>::const_reverse_iterator encloser_last_search;

  virtual void flush_hunk(size_t pos) = 0;
  virtual void advance_to(size_t newpos) = 0;
  virtual void insert_at(size_t b_pos) = 0;
  virtual void delete_at(size_t a_pos) = 0;
  virtual void find_encloser(size_t pos, string & encloser);
  virtual ~hunk_consumer() {}
  hunk_consumer(vector<string> const & a,
                vector<string> const & b,
                size_t ctx,
                ostream & ost,
                string const & encloser_pattern)
    : a(a), b(b), ctx(ctx), ost(ost), encloser_re(0),
      a_begin(0), b_begin(0), a_len(0), b_len(0), skew(0),
      encloser_last_match(a.rend()), encloser_last_search(a.rend())
  {
    if (encloser_pattern != "")
      encloser_re.reset(new pcre::regex(encloser_pattern));
  }
};

/* Find, and write to ENCLOSER, the nearest line before POS which matches
   ENCLOSER_PATTERN.  We remember the last line scanned, and the matched, to
   avoid duplication of effort.  */

void
hunk_consumer::find_encloser(size_t pos, string & encloser)
{
  typedef vector<string>::const_reverse_iterator riter;

  // Precondition: encloser_last_search <= pos <= a.size().
  I(pos <= a.size());
  // static_cast<> to silence compiler unsigned vs. signed comparison
  // warning, after first making sure that the static_cast is safe.
  I(a.rend() - encloser_last_search >= 0);
  I(pos >= static_cast<size_t>(a.rend() - encloser_last_search));

  if (!encloser_re)
    return;

  riter last = encloser_last_search;
  riter i    = riter(a.begin() + pos);

  encloser_last_search = i;

  // i is a reverse_iterator, so this loop goes backward through the vector.
  for (; i != last; i++)
    if (encloser_re->match(*i))
      {
        encloser_last_match = i;
        break;
      }

  if (encloser_last_match == a.rend())
    return;

  L(FL("find_encloser: from %u matching %d, \"%s\"")
    % pos % (a.rend() - encloser_last_match) % *encloser_last_match);

  // the number 40 is chosen to match GNU diff.  it could safely be
  // increased up to about 60 without overflowing the standard
  // terminal width.
  encloser = string(" ") + (*encloser_last_match).substr(0, 40);
}

void walk_hunk_consumer(vector<long, QA(long)> const & lcs,
                        vector<long, QA(long)> const & lines1,
                        vector<long, QA(long)> const & lines2,
                        hunk_consumer & cons)
{

  size_t a = 0, b = 0;
  if (lcs.begin() == lcs.end())
    {
      // degenerate case: files have nothing in common
      cons.advance_to(0);
      while (a < lines1.size())
        cons.delete_at(a++);
      while (b < lines2.size())
        cons.insert_at(b++);
      cons.flush_hunk(a);
    }
  else
    {
      // normal case: files have something in common
      for (vector<long, QA(long)>::const_iterator i = lcs.begin();
           i != lcs.end(); ++i, ++a, ++b)
        {
          if (idx(lines1, a) == *i && idx(lines2, b) == *i)
            continue;

          cons.advance_to(a);
          while (idx(lines1,a) != *i)
              cons.delete_at(a++);
          while (idx(lines2,b) != *i)
            cons.insert_at(b++);
        }
      if (a < lines1.size())
        {
          cons.advance_to(a);
          while(a < lines1.size())
            cons.delete_at(a++);
        }
      if (b < lines2.size())
        {
          cons.advance_to(a);
          while(b < lines2.size())
            cons.insert_at(b++);
        }
      cons.flush_hunk(a);
    }
}

struct unidiff_hunk_writer : public hunk_consumer
{
  vector<string> hunk;

  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  virtual ~unidiff_hunk_writer() {}
  unidiff_hunk_writer(vector<string> const & a,
                      vector<string> const & b,
                      size_t ctx,
                      ostream & ost,
                      string const & encloser_pattern)
  : hunk_consumer(a, b, ctx, ost, encloser_pattern)
  {}
};

void unidiff_hunk_writer::insert_at(size_t b_pos)
{
  b_len++;
  hunk.push_back(string("+") + b[b_pos]);
}

void unidiff_hunk_writer::delete_at(size_t a_pos)
{
  a_len++;
  hunk.push_back(string("-") + a[a_pos]);
}

void unidiff_hunk_writer::flush_hunk(size_t pos)
{
  if (!hunk.empty())
    {
      // insert trailing context
      size_t a_pos = a_begin + a_len;
      for (size_t i = 0; (i < ctx) && (a_pos + i < a.size()); ++i)
        {
          hunk.push_back(string(" ") + a[a_pos + i]);
          a_len++;
          b_len++;
        }

      // write hunk to stream
      if (a_len == 0)
        ost << "@@ -0,0";
      else
        {
          ost << "@@ -" << a_begin+1;
          if (a_len > 1)
            ost << ',' << a_len;
        }

      if (b_len == 0)
        ost << " +0,0";
      else
        {
          ost << " +" << b_begin+1;
          if (b_len > 1)
            ost << ',' << b_len;
        }

      {
        string encloser;
        ptrdiff_t first_mod = 0;
        vector<string>::const_iterator i;
        for (i = hunk.begin(); i != hunk.end(); i++)
          if ((*i)[0] != ' ')
            {
              first_mod = i - hunk.begin();
              break;
            }

        find_encloser(a_begin + first_mod, encloser);
        ost << " @@" << encloser << '\n';
      }
      copy(hunk.begin(), hunk.end(), ostream_iterator<string>(ost, "\n"));
    }

  // reset hunk
  hunk.clear();
  skew += b_len - a_len;
  a_begin = pos;
  b_begin = pos + skew;
  a_len = 0;
  b_len = 0;
}

void unidiff_hunk_writer::advance_to(size_t newpos)
{
  if (a_begin + a_len + (2 * ctx) < newpos || hunk.empty())
    {
      flush_hunk(newpos);

      // insert new leading context
      for (size_t p = max(ctx, newpos) - ctx;
           p < min(a.size(), newpos); ++p)
        {
          hunk.push_back(string(" ") + a[p]);
          a_begin--; a_len++;
          b_begin--; b_len++;
        }
    }
  else
    {
      // pad intermediate context
      while(a_begin + a_len < newpos)
        {
          hunk.push_back(string(" ") + a[a_begin + a_len]);
          a_len++;
          b_len++;
        }
    }
}

struct cxtdiff_hunk_writer : public hunk_consumer
{
  // For context diffs, we have to queue up calls to insert_at/delete_at
  // until we hit an advance_to, so that we can get the tags right: an
  // unpaired insert gets a + in the left margin, an unpaired delete a -,
  // but if they are paired, they both get !.  Hence, we have both the
  // 'inserts' and 'deletes' queues of line numbers, and the 'from_file' and
  // 'to_file' queues of line strings.
  vector<size_t> inserts;
  vector<size_t> deletes;
  vector<string> from_file;
  vector<string> to_file;
  bool have_insertions;
  bool have_deletions;

  virtual void flush_hunk(size_t pos);
  virtual void advance_to(size_t newpos);
  virtual void insert_at(size_t b_pos);
  virtual void delete_at(size_t a_pos);
  void flush_pending_mods();
  virtual ~cxtdiff_hunk_writer() {}
  cxtdiff_hunk_writer(vector<string> const & a,
                      vector<string> const & b,
                      size_t ctx,
                      ostream & ost,
                      string const & encloser_pattern)
  : hunk_consumer(a, b, ctx, ost, encloser_pattern),
    have_insertions(false), have_deletions(false)
  {}
};

void cxtdiff_hunk_writer::insert_at(size_t b_pos)
{
  inserts.push_back(b_pos);
  have_insertions = true;
}

void cxtdiff_hunk_writer::delete_at(size_t a_pos)
{
  deletes.push_back(a_pos);
  have_deletions = true;
}

void cxtdiff_hunk_writer::flush_hunk(size_t pos)
{
  flush_pending_mods();

  if (have_deletions || have_insertions)
    {
      // insert trailing context
      size_t ctx_start = a_begin + a_len;
      for (size_t i = 0; (i < ctx) && (ctx_start + i < a.size()); ++i)
        {
          from_file.push_back(string("  ") + a[ctx_start + i]);
          a_len++;
        }

      ctx_start = b_begin + b_len;
      for (size_t i = 0; (i < ctx) && (ctx_start + i < b.size()); ++i)
        {
          to_file.push_back(string("  ") + b[ctx_start + i]);
          b_len++;
        }

      {
        string encloser;
        ptrdiff_t first_insert = b_len;
        ptrdiff_t first_delete = a_len;
        vector<string>::const_iterator i;

        if (have_deletions)
          for (i = from_file.begin(); i != from_file.end(); i++)
            if ((*i)[0] != ' ')
              {
                first_delete = i - from_file.begin();
                break;
              }
        if (have_insertions)
          for (i = to_file.begin(); i != to_file.end(); i++)
            if ((*i)[0] != ' ')
              {
                first_insert = i - to_file.begin();
                break;
              }

        find_encloser(a_begin + min(first_insert, first_delete),
                      encloser);

        ost << "***************" << encloser << '\n';
      }

      ost << "*** " << (a_begin + 1) << ',' << (a_begin + a_len) << " ****\n";
      if (have_deletions)
        copy(from_file.begin(), from_file.end(), ostream_iterator<string>(ost, "\n"));

      ost << "--- " << (b_begin + 1) << ',' << (b_begin + b_len) << " ----\n";
      if (have_insertions)
        copy(to_file.begin(), to_file.end(), ostream_iterator<string>(ost, "\n"));
    }

  // reset hunk
  to_file.clear();
  from_file.clear();
  have_insertions = false;
  have_deletions = false;
  skew += b_len - a_len;
  a_begin = pos;
  b_begin = pos + skew;
  a_len = 0;
  b_len = 0;
}

void cxtdiff_hunk_writer::flush_pending_mods()
{
  // nothing to flush?
  if (inserts.empty() && deletes.empty())
    return;

  string prefix;

  // if we have just insertions to flush, prefix them with "+"; if
  // just deletions, prefix with "-"; if both, prefix with "!"
  if (inserts.empty() && !deletes.empty())
    prefix = "-";
  else if (deletes.empty() && !inserts.empty())
    prefix = "+";
  else
    prefix = "!";

  for (vector<size_t>::const_iterator i = deletes.begin();
       i != deletes.end(); ++i)
    {
      from_file.push_back(prefix + string(" ") + a[*i]);
      a_len++;
    }
  for (vector<size_t>::const_iterator i = inserts.begin();
       i != inserts.end(); ++i)
    {
      to_file.push_back(prefix + string(" ") + b[*i]);
      b_len++;
    }

  // clear pending mods
  inserts.clear();
  deletes.clear();
}

void cxtdiff_hunk_writer::advance_to(size_t newpos)
{
  // We must first flush out pending mods because otherwise our calculation
  // of whether we need to generate a new hunk header will be way off.
  // It is correct (i.e. consistent with diff(1)) to reset the +/-/!
  // generation algorithm between sub-components of a single hunk.
  flush_pending_mods();

  if (a_begin + a_len + (2 * ctx) < newpos)
    {
      flush_hunk(newpos);

      // insert new leading context
      if (newpos - ctx < a.size())
        {
          for (size_t i = ctx; i > 0; --i)
            {
              // The original test was (newpos - i < 0), but since newpos
              // is size_t (unsigned), it will never go negative.  Testing
              // that newpos is smaller than i is the same test, really.
              if (newpos < i)
                continue;

              // note that context diffs prefix common text with two
              // spaces, whereas unified diffs use a single space
              from_file.push_back(string("  ") + a[newpos - i]);
              to_file.push_back(string("  ") + a[newpos - i]);
              a_begin--; a_len++;
              b_begin--; b_len++;
            }
        }
    }
  else
    // pad intermediate context
    while (a_begin + a_len < newpos)
      {
        from_file.push_back(string("  ") + a[a_begin + a_len]);
        to_file.push_back(string("  ") + a[a_begin + a_len]);
        a_len++;
        b_len++;
      }
}

void
make_diff(string const & filename1,
          string const & filename2,
          file_id const & id1,
          file_id const & id2,
          data const & data1,
          data const & data2,
          ostream & ost,
          diff_type type,
          string const & pattern)
{
  if (guess_binary(data1()) || guess_binary(data2()))
    {
      ost << "# " << filename2 << " is binary\n";
      return;
    }

  vector<string> lines1, lines2;
  split_into_lines(data1(), lines1, true);
  split_into_lines(data2(), lines2, true);

  vector<long, QA(long)> left_interned;
  vector<long, QA(long)> right_interned;
  vector<long, QA(long)> lcs;

  interner<long> in;

  left_interned.reserve(lines1.size());
  for (vector<string>::const_iterator i = lines1.begin();
       i != lines1.end(); ++i)
    left_interned.push_back(in.intern(*i));

  right_interned.reserve(lines2.size());
  for (vector<string>::const_iterator i = lines2.begin();
       i != lines2.end(); ++i)
    right_interned.push_back(in.intern(*i));

  lcs.reserve(min(lines1.size(),lines2.size()));
  longest_common_subsequence(left_interned.begin(), left_interned.end(),
                             right_interned.begin(), right_interned.end(),
                             min(lines1.size(), lines2.size()),
                             back_inserter(lcs));

  // The existence of various hacky diff parsers in the world somewhat
  // constrains what output we can use.  Here are some notes on how various
  // tools interpret the header lines of a diff file:
  //
  // interdiff/filterdiff (patchutils):
  //   Attempt to parse a timestamp after each whitespace.  If they succeed,
  //   then they take the filename as everything up to the whitespace they
  //   succeeded at, and the timestamp as everything after.  If they fail,
  //   then they take the filename to be everything up to the first
  //   whitespace.  Have hardcoded that /dev/null and timestamps at the
  //   epoch (in any timezone) indicate a file that did not exist.
  //
  //   filterdiff filters on the first filename line.  interdiff matches on
  //   the first filename line.
  // PatchReader perl library (used by Bugzilla):
  //   Takes the filename to be everything up to the first tab; requires
  //   that there be a tab.  Determines the filename based on the first
  //   filename line.
  // diffstat:
  //   Can handle pretty much everything; tries to read up to the first tab
  //   to get the filename.  Knows that "/dev/null", "", and anything
  //   beginning "/tmp/" are meaningless.  Uses the second filename line.
  // patch:
  //   If there is a tab, considers everything up to that tab to be the
  //   filename.  If there is not a tab, considers everything up to the
  //   first whitespace to be the filename.
  //
  //   Contains comment: 'If the [file]name is "/dev/null", ignore the name
  //   and mark the file as being nonexistent.  The name "/dev/null" appears
  //   in patches regardless of how NULL_DEVICE is spelled.'  Also detects
  //   timestamps at the epoch as indicating that a file does not exist.
  //
  //   Uses the first filename line as the target, unless it is /dev/null or
  //   has an epoch timestamp in which case it uses the second.
  // trac:
  //   Anything up to the first whitespace, or end of line, is considered
  //   filename.  Does not care about timestamp.  Uses the shorter of the
  //   two filenames as the filename (!).
  //
  // Conclusions:
  //   -- You must have a tab, both to prevent PatchReader blowing up, and
  //      to make it possible to have filenames with spaces in them.
  //      (Filenames with tabs in them are always impossible to properly
  //      express; FIXME what should be done if one occurs?)
  //   -- What comes after that tab matters not at all, though it probably
  //      shouldn't look like a timestamp, or have any trailing part that
  //      looks like a timestamp, unless it really is a timestamp.  Simply
  //      having a trailing tab should work fine.
  //   -- If you need to express that some file does not exist, you should
  //      use /dev/null as the path.  patch(1) goes so far as to claim that
  //      this is part of the diff format definition.
  //   -- If you want your patches to actually _work_ with patch(1), then
  //      renames are basically hopeless (you can do them by hand _after_
  //      running patch), adds work so long as the first line says either
  //      the new file's name or "/dev/null", nothing else, and deletes work
  //      if the new file name is "/dev/null", nothing else.  (ATM we don't
  //      write out patches for deletes anyway.)
  switch (type)
    {
      case unified_diff:
      {
        ost << "--- " << filename1 << '\t'
            << id1 << '\n';
        ost << "+++ " << filename2 << '\t'
            << id2 << '\n';

        unidiff_hunk_writer hunks(lines1, lines2, 3, ost, pattern);
        walk_hunk_consumer(lcs, left_interned, right_interned, hunks);
        break;
      }
      case context_diff:
      {
        ost << "*** " << filename1 << '\t'
            << id1 << '\n';
        ost << "--- " << filename2 << '\t'
            << id2 << '\n';

        cxtdiff_hunk_writer hunks(lines1, lines2, 3, ost, pattern);
        walk_hunk_consumer(lcs, left_interned, right_interned, hunks);
        break;
      }
      default:
      {
        // should never reach this; the external_diff type is not
        // handled by this function.
        I(false);
      }
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "lexical_cast.hh"
#include "randomfile.hh"

using std::cerr;
using std::cout;
using std::stringstream;

using boost::lexical_cast;

static void dump_incorrect_merge(vector<string> const & expected,
                                 vector<string> const & got,
                                 string const & prefix)
{
  size_t mx = expected.size();
  if (mx < got.size())
    mx = got.size();
  for (size_t i = 0; i < mx; ++i)
    {
      cerr << "bad merge: " << i << " [" << prefix << "]\t";

      if (i < expected.size())
        cerr << '[' << expected[i] << "]\t";
      else
        cerr << "[--nil--]\t";

      if (i < got.size())
        cerr << '[' << got[i] << "]\t";
      else
        cerr << "[--nil--]\t";

      cerr << '\n';
    }
}

// high tech randomizing test
UNIT_TEST(diff_patch, randomizing_merge)
{
  randomizer rng;
  for (int i = 0; i < 30; ++i)
    {
      vector<string> anc, d1, d2, m1, m2, gm;

      file_randomizer::build_random_fork(anc, d1, d2, gm, (10 + 2 * i), rng);

      UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
      if (gm != m1)
        dump_incorrect_merge (gm, m1, "random_merge 1");
      UNIT_TEST_CHECK(gm == m1);

      UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
      if (gm != m2)
        dump_incorrect_merge (gm, m2, "random_merge 2");
      UNIT_TEST_CHECK(gm == m2);
    }
}


// old boring tests
UNIT_TEST(diff_patch, merge_prepend)
{
  UNIT_TEST_CHECKPOINT("prepend test");
  vector<string> anc, d1, d2, m1, m2, gm;
  for (int i = 10; i < 20; ++i)
    {
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  for (int i = 0; i < 10; ++i)
    {
      anc.push_back(lexical_cast<string>(i));
      d1.push_back(lexical_cast<string>(i));
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_prepend 1");
  UNIT_TEST_CHECK(gm == m1);


  UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_prepend 2");
  UNIT_TEST_CHECK(gm == m2);
}

UNIT_TEST(diff_patch, merge_append)
{
  UNIT_TEST_CHECKPOINT("append test");
  vector<string> anc, d1, d2, m1, m2, gm;
  for (int i = 0; i < 10; ++i)
      anc.push_back(lexical_cast<string>(i));

  d1 = anc;
  d2 = anc;
  gm = anc;

  for (int i = 10; i < 20; ++i)
    {
      d2.push_back(lexical_cast<string>(i));
      gm.push_back(lexical_cast<string>(i));
    }

  UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_append 1");
  UNIT_TEST_CHECK(gm == m1);

  UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_append 2");
  UNIT_TEST_CHECK(gm == m2);


}

UNIT_TEST(diff_patch, merge_additions)
{
  UNIT_TEST_CHECKPOINT("additions test");
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc1("I like oatmeal\nI don't like spam\nI like orange juice\nI like toast");
  string confl("I like oatmeal\nI don't like tuna\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like orange juice\nI don't like tuna\nI like toast");
  string good_merge("I like oatmeal\nI don't like spam\nI like orange juice\nI don't like tuna\nI like toast");
  vector<string> anc, d1, cf, d2, m1, m2, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc1, d1);
  split_into_lines(confl, cf);
  split_into_lines(desc2, d2);
  split_into_lines(good_merge, gm);

  UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_addition 1");
  UNIT_TEST_CHECK(gm == m1);

  UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_addition 2");
  UNIT_TEST_CHECK(gm == m2);

  UNIT_TEST_CHECK(!merge3(anc, d1, cf, m1));
}

UNIT_TEST(diff_patch, merge_deletions)
{
  string ancestor("I like oatmeal\nI like orange juice\nI like toast");
  string desc2("I like oatmeal\nI like toast");

  vector<string> anc, d1, d2, m1, m2, gm;

  split_into_lines(ancestor, anc);
  split_into_lines(desc2, d2);
  d1 = anc;
  gm = d2;

  UNIT_TEST_CHECK(merge3(anc, d1, d2, m1));
  if (gm != m1)
    dump_incorrect_merge (gm, m1, "merge_deletion 1");
  UNIT_TEST_CHECK(gm == m1);

  UNIT_TEST_CHECK(merge3(anc, d2, d1, m2));
  if (gm != m2)
    dump_incorrect_merge (gm, m2, "merge_deletion 2");
  UNIT_TEST_CHECK(gm == m2);
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
