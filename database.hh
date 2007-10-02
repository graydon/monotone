#ifndef __DATABASE_HH__
#define __DATABASE_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

struct sqlite3;
struct sqlite3_stmt;
struct cert;
int sqlite3_finalize(sqlite3_stmt *);

#include "vector.hh"
#include <set>
#include <map>

#include "numeric_vocab.hh"
#include "vocab.hh"
#include "paths.hh"
#include "cleanup.hh"
#include "roster.hh"
#include "selectors.hh"
#include "graph.hh"

// FIXME: would be better not to include this everywhere
#include "outdated_indicator.hh"
#include "lru_writeback_cache.hh"

// this file defines a public, typed interface to the database.
// the database class encapsulates all knowledge about sqlite,
// the schema, and all SQL statements used to access the schema.
//
// one thing which is rather important to note is that this file
// deals with two sorts of version relationships. the versions
// stored in the database are all *backwards* from those the program
// sees. so for example if you have two versions of a file
//
// file.1, file.2
//
// where file.2 was a modification of file.1, then as far as the rest of
// the application is concerned -- and the ancestry graph -- file.1 is the
// "old" version and file.2 is the "new" version. note the use of terms
// which describe time, and the sequence of edits a user makes to a
// file. those are ancestry terms. when the application composes a
// patchset, for example, it'll contain the diff delta(file.1, file.2)
//
// from the database's perspective, however, file.1 is the derived version,
// and file.2 is the base version. the base version is stored in the
// "files" table, and the *reverse* diff delta(file.2, file.1) is stored in
// the "file_deltas" table, under the id of file.1, with the id of file.2
// listed as its base. note the use of the terms which describe
// reconstruction; those are storage-system terms.
//
// the interface *to* the database, and the ancestry version graphs, use
// the old / new metaphor of ancestry, but within the database (including
// the private helper methods, and the storage version graphs) the
// base/derived storage metaphor is used. the only real way to tell which
// is which is to look at the parameter names and code. I might try to
// express this in the type system some day, but not presently.
//
// the key phrase to keep repeating when working on this code is:
//
// "base files are new, derived files are old"
//
// it makes the code confusing, I know. this is possibly the worst part of
// the program. I don't know if there's any way to make it clearer.

class transaction_guard;
class app_state;
struct revision_t;
struct query;
class rev_height;
struct globish;

class database
{
  //
  // --== Opening the database and schema checking ==--
  //
private:
  system_path filename;
  app_state * __app;
  struct sqlite3 * __sql;

  enum open_mode { normal_mode = 0,
                   schema_bypass_mode,
                   format_bypass_mode };

  void install_functions(app_state * app);
  struct sqlite3 * sql(enum open_mode mode = normal_mode);

  void check_filename();
  void check_db_exists();
  void check_db_nonexistent();
  void open();
  void close();
  void check_format();

public:
  database(system_path const & file);
  ~database();

  void set_app(app_state * app);

  void set_filename(system_path const & file);
  system_path get_filename();
  bool is_dbfile(any_path const & file);
  void ensure_open();
  void ensure_open_for_format_changes();
private:
  void ensure_open_for_maintenance();
public:
  void check_is_not_rosterified();
  bool database_specified();

  //
  // --== Basic SQL interface and statement caching ==--
  //
private:
  struct statement {
    statement() : count(0), stmt(0, sqlite3_finalize) {}
    int count;
    cleanup_ptr<sqlite3_stmt*, int> stmt;
  };
  std::map<std::string, statement> statement_cache;

  typedef std::vector< std::vector<std::string> > results;
  void fetch(results & res,
             int const want_cols, int const want_rows,
             query const & q);
  void execute(query const & q);

  bool table_has_entry(std::string const & key, std::string const & column,
                       std::string const & table);

  //
  // --== Generic database metadata gathering ==--
  //
private:
  std::string count(std::string const & table);
  std::string space(std::string const & table,
                    std::string const & concatenated_columns,
                    u64 & total);
  unsigned int page_size();
  unsigned int cache_size();

  //
  // --== Transactions ==--
  //
private:
  int transaction_level;
  bool transaction_exclusive;
  void begin_transaction(bool exclusive);
  void commit_transaction();
  void rollback_transaction();
  friend class transaction_guard;

  //
  // --== Write-buffering -- tied into transaction  ==--
  // --== machinery and delta compression machinery ==--
  //
public:
  typedef boost::shared_ptr<roster_t const> roster_t_cp;
  typedef boost::shared_ptr<marking_map const> marking_map_cp;
  typedef std::pair<roster_t_cp, marking_map_cp> cached_roster;
private:
  struct roster_size_estimator
  {
    unsigned long operator() (cached_roster const &);
  };
  struct roster_writeback_manager
  {
    database & db;
    roster_writeback_manager(database & db) : db(db) {}
    void writeout(revision_id const &, cached_roster const &);
  };
  LRUWritebackCache<revision_id, cached_roster,
                    roster_size_estimator, roster_writeback_manager>
    roster_cache;

  size_t size_delayed_file(file_id const & id, file_data const & dat);
  bool have_delayed_file(file_id const & id);
  void load_delayed_file(file_id const & id, file_data & dat);
  void cancel_delayed_file(file_id const & id);
  void drop_or_cancel_file(file_id const & id);
  void schedule_delayed_file(file_id const & id, file_data const & dat);

  std::map<file_id, file_data> delayed_files;
  size_t delayed_writes_size;

  void flush_delayed_writes();
  void clear_delayed_writes();
  void write_delayed_file(file_id const & new_id,
                          file_data const & dat);

  void write_delayed_roster(revision_id const & new_id,
                            roster_t const & roster,
                            marking_map const & marking);

  //
  // --== Reading/writing delta-compressed objects ==--
  //
private:
  // "do we have any entry for 'ident' that is a base version"
  bool file_or_manifest_base_exists(hexenc<id> const & ident,
                                    std::string const & table);
  bool roster_base_stored(revision_id const & ident);
  bool roster_base_available(revision_id const & ident);
  
  // "do we have any entry for 'ident' that is a delta"
  bool delta_exists(std::string const & ident,
                    std::string const & table);

  bool delta_exists(std::string const & ident,
                    std::string const & base,
                    std::string const & table);

  void get_file_or_manifest_base_unchecked(hexenc<id> const & new_id,
                                           data & dat,
                                           std::string const & table);
  void get_file_or_manifest_delta_unchecked(hexenc<id> const & ident,
                                            hexenc<id> const & base,
                                            delta & del,
                                            std::string const & table);
  void get_roster_base(std::string const & ident,
                       roster_t & roster, marking_map & marking);
  void get_roster_delta(std::string const & ident,
                        std::string const & base,
                        roster_delta & del);
  friend struct file_and_manifest_reconstruction_graph;
  friend struct roster_reconstruction_graph;
  void get_version(hexenc<id> const & ident,
                   data & dat,
                   std::string const & data_table,
                   std::string const & delta_table);

  void drop(std::string const & base,
            std::string const & table);
  void put_file_delta(file_id const & ident,
                      file_id const & base,
                      file_delta const & del);
private:
  void put_roster_delta(revision_id const & ident,
                        revision_id const & base,
                        roster_delta const & del);
  void put_version(hexenc<id> const & old_id,
                   hexenc<id> const & new_id,
                   delta const & del,
                   std::string const & data_table,
                   std::string const & delta_table);

  void put_roster(revision_id const & rev_id,
                  roster_t_cp const & roster,
                  marking_map_cp const & marking);

public:

  bool file_version_exists(file_id const & ident);
  bool revision_exists(revision_id const & ident);
  bool roster_link_exists_for_revision(revision_id const & ident);
  bool roster_exists_for_revision(revision_id const & ident);


  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist)
  void get_file_version(file_id const & ident,
                        file_data & dat);

  // put file w/o predecessor into db
  void put_file(file_id const & new_id,
                file_data const & dat);

  // store new version and update old version to be a delta
  void put_file_version(file_id const & old_id,
                        file_id const & new_id,
                        file_delta const & del);

  void get_arbitrary_file_delta(file_id const & src_id,
                                file_id const & dst_id,
                                file_delta & del);

  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist).
  void get_manifest_version(manifest_id const & ident,
                            manifest_data & dat);

  //
  // --== The ancestry graph ==--
  //
public:
  void get_revision_ancestry(rev_ancestry_map & graph);

  void get_revision_parents(revision_id const & ident,
                           std::set<revision_id> & parents);

  void get_revision_children(revision_id const & ident,
                             std::set<revision_id> & children);

  void get_revision_manifest(revision_id const & cid,
                             manifest_id & mid);
private:
  // helper
  void get_ids(std::string const & table, std::set< hexenc<id> > & ids);
public:
  void get_revision_ids(std::set<revision_id> & ids);
  // this is exposed for 'db check':
  void get_file_ids(std::set<file_id> & ids);

  //
  // --== Revision reading/writing ==--
  //
private:
  void deltify_revision(revision_id const & rid);
public:
  void get_revision(revision_id const & ident,
                    revision_t & cs);

  void get_revision(revision_id const & ident,
                    revision_data & dat);

  bool put_revision(revision_id const & new_id,
                    revision_t const & rev);

  bool put_revision(revision_id const & new_id,
                    revision_data const & dat);

  //
  // --== Rosters ==--
  //
private:
  u64 next_id_from_table(std::string const & table);
  void get_roster_version(revision_id const & ros_id, cached_roster & cr);
public:
  node_id next_node_id();

  void get_roster(revision_id const & rid,
                  roster_t & roster);

  void get_roster(revision_id const & rid,
                  roster_t & roster,
                  marking_map & marks);

  void get_roster(revision_id const & rid,
                  cached_roster & cr);

  // these are exposed for the use of database_check.cc
  bool roster_version_exists(revision_id const & ident);
  void get_roster_ids(std::set<revision_id> & ids);

  // using roster deltas
  void get_markings(revision_id const & id,
                    node_id const & nid,
                    marking_t & markings);

  void get_file_content(revision_id const & id,
                        node_id const & nid,
                        file_id & content);
private:
  struct extractor;
  struct file_content_extractor;
  struct markings_extractor;
  void extract_from_deltas(revision_id const & id, extractor & x);

  //
  // --== Keys ==--
  //
private:
  void get_keys(std::string const & table, std::vector<rsa_keypair_id> & keys);
public:
  void get_key_ids(std::vector<rsa_keypair_id> & pubkeys);
  void get_key_ids(globish const & pattern,
                   std::vector<rsa_keypair_id> & pubkeys);

  void get_public_keys(std::vector<rsa_keypair_id> & pubkeys);

  bool public_key_exists(hexenc<id> const & hash);
  bool public_key_exists(rsa_keypair_id const & ident);

  void get_pubkey(hexenc<id> const & hash,
                  rsa_keypair_id & ident,
                  base64<rsa_pub_key> & pub_encoded);

  void get_key(rsa_keypair_id const & ident,
               base64<rsa_pub_key> & pub_encoded);

  bool put_key(rsa_keypair_id const & ident,
               base64<rsa_pub_key> const & pub_encoded);

  void delete_public_key(rsa_keypair_id const & pub_id);

  //
  // --== Certs ==--
  //
  // note: this section is ridiculous. please do something about it.
private:
  bool cert_exists(cert const & t,
                   std::string const & table);
  void put_cert(cert const & t, std::string const & table);
  void results_to_certs(results const & res,
                        std::vector<cert> & certs);
  
  void get_certs(std::vector< cert > & certs,
                 std::string const & table);
  
  void get_certs(hexenc<id> const & ident,
                 std::vector< cert > & certs,
                 std::string const & table);

  void get_certs(cert_name const & name,
                 std::vector< cert > & certs,
                 std::string const & table);

  void get_certs(hexenc<id> const & ident,
                 cert_name const & name,
                 std::vector< cert > & certs,
                 std::string const & table);

  void get_certs(hexenc<id> const & ident,
                 cert_name const & name,
                 base64<cert_value> const & val,
                 std::vector< cert > & certs,
                 std::string const & table);

  void get_certs(cert_name const & name,
                 base64<cert_value> const & val,
                 std::vector<cert> & certs,
                 std::string const & table);

  outdated_indicator_factory cert_stamper;
public:

  bool revision_cert_exists(revision<cert> const & cert);
  bool revision_cert_exists(hexenc<id> const & hash);

  bool put_revision_cert(revision<cert> const & cert);

  // this variant has to be rather coarse and fast, for netsync's use
  outdated_indicator get_revision_cert_nobranch_index(std::vector< std::pair<hexenc<id>,
                              std::pair<revision_id, rsa_keypair_id> > > & idx);

  // Only used by database_check.cc
  outdated_indicator get_revision_certs(std::vector< revision<cert> > & certs);

  outdated_indicator get_revision_certs(cert_name const & name,
                          std::vector< revision<cert> > & certs);

  outdated_indicator get_revision_certs(revision_id const & ident,
                          cert_name const & name,
                          std::vector< revision<cert> > & certs);

  // Only used by get_branch_certs (project.cc)
  outdated_indicator get_revision_certs(cert_name const & name,
                          base64<cert_value> const & val,
                          std::vector< revision<cert> > & certs);

  // Only used by revision_is_in_branch (project.cc)
  outdated_indicator get_revision_certs(revision_id const & ident,
                          cert_name const & name,
                          base64<cert_value> const & value,
                          std::vector< revision<cert> > & certs);

  // Only used by get_branch_heads (project.cc)
  outdated_indicator get_revisions_with_cert(cert_name const & name,
                               base64<cert_value> const & value,
                               std::set<revision_id> & revisions);

  // Used through project.cc, and by
  // anc_graph::add_node_for_oldstyle_revision (revision.cc)
  outdated_indicator get_revision_certs(revision_id const & ident,
                          std::vector< revision<cert> > & certs);

  // Used through get_revision_cert_hashes (project.cc)
  outdated_indicator get_revision_certs(revision_id const & ident,
                          std::vector< hexenc<id> > & hashes);

  void get_revision_cert(hexenc<id> const & hash,
                         revision<cert> & c);

  void get_manifest_certs(manifest_id const & ident,
                          std::vector< manifest<cert> > & certs);

  void get_manifest_certs(cert_name const & name,
                          std::vector< manifest<cert> > & certs);

  //
  // --== Epochs ==--
  //
public:
  void get_epochs(std::map<branch_name, epoch_data> & epochs);

  void get_epoch(epoch_id const & eid, branch_name & branch, epoch_data & epo);

  bool epoch_exists(epoch_id const & eid);

  void set_epoch(branch_name const & branch, epoch_data const & epo);

  void clear_epoch(branch_name const & branch);

  //
  // --== Database 'vars' ==--
  //
public:
  void get_vars(std::map<var_key, var_value > & vars);

  void get_var(var_key const & key, var_value & value);

  bool var_exists(var_key const & key);

  void set_var(var_key const & key, var_value const & value);

  void clear_var(var_key const & key);

  //
  // --== Completion ==--
  //
public:
  void complete(std::string const & partial,
                std::set<revision_id> & completions);

  void complete(std::string const & partial,
                std::set<file_id> & completions);

  void complete(std::string const & partial,
                std::set< std::pair<key_id, utf8 > > & completions);

  void complete(selectors::selector_type ty,
                std::string const & partial,
                std::vector<std::pair<selectors::selector_type,
                                      std::string> > const & limit,
                std::set<std::string> & completions);

  //
  // --== The 'db' family of top-level commands ==--
  //
public:
  void initialize();
  void debug(std::string const & sql, std::ostream & out);
  void dump(std::ostream &);
  void load(std::istream &);
  void info(std::ostream &);
  void version(std::ostream &);
  void migrate();
  void test_migration_step(std::string const &);
  // for kill_rev_locally:
  void delete_existing_rev_and_certs(revision_id const & rid);
  // for kill_branch_certs_locally:
  void delete_branch_named(cert_value const & branch);
  // for kill_tag_locally:
  void delete_tag_named(cert_value const & tag);

  // misc
private:
  friend void rcs_put_raw_file_edge(hexenc<id> const & old_id,
                                    hexenc<id> const & new_id,
                                    delta const & del,
                                    database & db);
public:
    // branches
  outdated_indicator get_branches(std::vector<std::string> & names);
  outdated_indicator get_branches(globish const & glob,
                                  std::vector<std::string> & names);

  bool check_integrity();

  void get_uncommon_ancestors(revision_id const & a,
                              revision_id const & b,
                              std::set<revision_id> & a_uncommon_ancs,
                              std::set<revision_id> & b_uncommon_ancs);

  // for changesetify, rosterify
  void delete_existing_revs_and_certs();

  void delete_existing_manifests();

  // heights
  void get_rev_height(revision_id const & id,
                      rev_height & height);

  void put_rev_height(revision_id const & id,
                      rev_height const & height);
  
  bool has_rev_height(rev_height const & height);
  void delete_existing_heights();

  void put_height_for_revision(revision_id const & new_id,
                               revision_t const & rev);

  // for regenerate_rosters
  void delete_existing_rosters();
  void put_roster_for_revision(revision_id const & new_id,
                               revision_t const & rev);
};

// Parent maps are used in a number of places to keep track of all the
// parent rosters of a given revision.
typedef std::map<revision_id, database::cached_roster>
parent_map;

typedef parent_map::value_type
parent_entry;

inline revision_id const & parent_id(parent_entry const & p)
{
  return p.first;
}

inline revision_id const & parent_id(parent_map::const_iterator i)
{
  return i->first;
}

inline database::cached_roster const &
parent_cached_roster(parent_entry const & p)
{
  return p.second;
}

inline database::cached_roster const &
parent_cached_roster(parent_map::const_iterator i)
{
  return i->second;
}

inline roster_t const & parent_roster(parent_entry const & p)
{
  return *(p.second.first);
}

inline roster_t const & parent_roster(parent_map::const_iterator i)
{
  return *(i->second.first);
}

inline marking_map const & parent_marking(parent_entry const & p)
{
  return *(p.second.second);
}

inline marking_map const & parent_marking(parent_map::const_iterator i)
{
  return *(i->second.second);
}

// Transaction guards nest. Acquire one in any scope you'd like
// transaction-protected, and it'll make sure the db aborts a transaction
// if there's any exception before you call commit().
//
// By default, transaction_guard locks the database exclusively. If the
// transaction is intended to be read-only, construct the guard with
// exclusive=false. In this case, if a database update is attempted and
// another process is accessing the database an exception will be thrown -
// uglier and more confusing for the user - however no data inconsistency
// should result.
//
// An exception is thrown if an exclusive transaction_guard is created
// while a non-exclusive transaction_guard exists.
//
// Transaction guards also support splitting long transactions up into
// checkpoints. Any time you feel the database is in an
// acceptably-consistent state, you can call maybe_checkpoint(nn) with a
// given number of bytes. When the number of bytes and number of
// maybe_checkpoint() calls exceeds the guard's parameters, the transaction
// is committed and reopened. Any time you feel the database has reached a
// point where want to ensure a transaction commit, without destructing the
// object, you can call do_checkpoint().
//
// This does *not* free you from having to call .commit() on the guard when
// it "completes" its lifecycle. Here's a way to think of checkpointing: a
// normal transaction guard is associated with a program-control
// scope. Sometimes (notably in netsync) it is not convenient to create a
// scope which exactly matches the size of work-unit you want to commit (a
// bunch of packets, or a session-close, whichever comes first) so
// checkpointing allows you to use a long-lived transaction guard and mark
// off the moments where commits are desired, without destructing the
// guard. The guard still performs an error-management task in case of an
// exception, so you still have to clean it before destruction using
// .commit().
//
// Checkpointing also does not override the transaction guard nesting: if
// there's an enclosing transaction_guard, your checkpointing calls have no
// affect.
//
// The purpose of checkpointing is to provide an alternative to "many short
// transactions" on platforms (OSX in particular) where the overhead of
// full commits at high frequency is too high. The solution for these
// platforms is to run inside a longer-lived transaction (session-length),
// and checkpoint at higher granularity (every megabyte or so).

class transaction_guard
{
  bool committed;
  database & db;
  bool exclusive;
  size_t const checkpoint_batch_size;
  size_t const checkpoint_batch_bytes;
  size_t checkpointed_calls;
  size_t checkpointed_bytes;
public:
  transaction_guard(database & d, bool exclusive=true,
                    size_t checkpoint_batch_size=1000,
                    size_t checkpoint_batch_bytes=0xfffff);
  ~transaction_guard();
  void do_checkpoint();
  void maybe_checkpoint(size_t nbytes);
  void commit();
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __DATABASE_HH__
