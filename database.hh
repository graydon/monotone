#ifndef __DATABASE_HH__
#define __DATABASE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

struct sqlite3;
struct cert;

#include <vector>
#include <set>
#include <string>

#include <boost/filesystem/path.hpp>

#include "commands.hh"
#include "manifest.hh"
#include "numeric_vocab.hh"
#include "vocab.hh"

struct revision_set;

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
struct posting;
struct app_state;

class database
{
  fs::path filename;
  std::string const schema;
  void check_schema();

  struct app_state * __app;
  struct sqlite3 * __sql;
  struct sqlite3 * sql(bool init = false);
  int transaction_level;

  void install_functions(app_state * app);
  void install_views();

  typedef std::vector< std::vector<std::string> > results;
  void execute(char const * query, ...);
  void fetch(results & res, 
             int const want_cols, 
             int const want_rows, 
             char const * query, ...);

  bool exists(hexenc<id> const & ident, 
              std::string const & table);
  bool delta_exists(hexenc<id> const & ident,
                    std::string const & table);
  bool delta_exists(hexenc<id> const & ident,
                    hexenc<id> const & base,
                    std::string const & table);

  int count(std::string const & table);

  void get(hexenc<id> const & new_id,
           base64< gzip<data> > & dat,
           std::string const & table);
  void get_delta(hexenc<id> const & ident,
                 hexenc<id> const & base,
                 base64< gzip<delta> > & del,
                 std::string const & table);
  void get_version(hexenc<id> const & id,
                   base64< gzip<data> > & dat,
                   std::string const & data_table,
                   std::string const & delta_table);
  
  void put(hexenc<id> const & new_id,
           base64< gzip<data> > const & dat,
           std::string const & table);
  void drop(hexenc<id> const & base,
            std::string const & table);
  void put_delta(hexenc<id> const & id,
                 hexenc<id> const & base,
                 base64< gzip<delta> > const & del,
                 std::string const & table);
  void put_version(hexenc<id> const & old_id,
                   hexenc<id> const & new_id,
                   base64< gzip<delta> > const & del,
                   std::string const & data_table,
                   std::string const & delta_table);
  void put_reverse_version(hexenc<id> const & new_id,
                           hexenc<id> const & old_id,
                           base64< gzip<delta> > const & reverse_del,
                           std::string const & data_table,
                           std::string const & delta_table);

  bool cert_exists(cert const & t,
                  std::string const & table);
  void put_cert(cert const & t, std::string const & table);  
  void results_to_certs(results const & res,
                       std::vector<cert> & certs);

  void get_certs(hexenc<id> const & id, 
                std::vector< cert > & certs,
                std::string const & table);  

  void get_certs(cert_name const & name,              
                std::vector< cert > & certs,
                std::string const & table);

  void get_certs(hexenc<id> const & id,
                cert_name const & name,
                std::vector< cert > & certs,
                std::string const & table);  

  void get_certs(hexenc<id> const & id,
                cert_name const & name,
                base64<cert_value> const & val, 
                std::vector< cert > & certs,
                std::string const & table);  

  void get_certs(cert_name const & name,
                base64<cert_value> const & val, 
                std::vector<cert> & certs,
                std::string const & table);

  void begin_transaction();
  void commit_transaction();
  void rollback_transaction();
  friend class transaction_guard;
  friend void rcs_put_raw_file_edge(hexenc<id> const & old_id,
                                    hexenc<id> const & new_id,
                                    base64< gzip<delta> > const & del,
                                    database & db);
  friend void rcs_put_raw_manifest_edge(hexenc<id> const & old_id,
                                        hexenc<id> const & new_id,
                                        base64< gzip<delta> > const & del,
                                        database & db);

public:

  database(fs::path const & file);

  unsigned long get_statistic(std::string const & query);
  void set_filename(fs::path const & file);
  void initialize();
  void debug(std::string const & sql, std::ostream & out);
  void dump(std::ostream &);
  void load(std::istream &);
  void info(std::ostream &);
  void version(std::ostream &);
  void migrate();
  void rehash();
  void ensure_open();
  
  bool file_version_exists(file_id const & id);
  bool manifest_version_exists(manifest_id const & id);
  bool revision_exists(revision_id const & id);
  void set_app(app_state * app);
  
  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist)
  void get_file_version(file_id const & id,
                        file_data & dat);

  // get file delta if it exists, else calculate it.
  // both manifests must exist.
  void get_file_delta(file_id const & src,
                      file_id const & dst,
                      file_delta & del);

  // put file w/o predecessor into db
  void put_file(file_id const & new_id,
                file_data const & dat);

  // store new version and update old version to be a delta
  void put_file_version(file_id const & old_id,
                        file_id const & new_id,
                        file_delta const & del);

  // load in a "direct" new -> old reverse edge (used during
  // netsync and CVS load-in)
  void put_file_reverse_version(file_id const & old_id,
                                file_id const & new_id,
                                file_delta const & del);

  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist). 
  void get_manifest_version(manifest_id const & id,
                            manifest_data & dat);

  // get a constructed manifest
  void get_manifest(manifest_id const & id,
                    manifest_map & mm);

  // get manifest delta if it exists, else calculate it.
  // both manifests must exist.
  void get_manifest_delta(manifest_id const & src,
                          manifest_id const & dst,
                          manifest_delta & del);

  // put manifest w/o predecessor into db
  void put_manifest(manifest_id const & new_id,
                    manifest_data const & dat);

  // store new version and update old version to be a delta
  void put_manifest_version(manifest_id const & old_id,
                            manifest_id const & new_id,
                            manifest_delta const & del);

  // load in a "direct" new -> old reverse edge (used during
  // netsync and CVS load-in)
  void put_manifest_reverse_version(manifest_id const & old_id,
                                    manifest_id const & new_id,
                                    manifest_delta const & del);


  void get_revision_ancestry(std::multimap<revision_id, revision_id> & graph);

  void get_revision_parents(revision_id const & id,
                           std::set<revision_id> & parents);

  void get_revision_children(revision_id const & id,
                             std::set<revision_id> & children);

  void get_revision_manifest(revision_id const & cid,
                            manifest_id & mid);

  void get_revision(revision_id const & id,
                   revision_set & cs);

  void get_revision(revision_id const & id,
                   revision_data & dat);

  void put_revision(revision_id const & new_id,
                   revision_set const & cs);

  void put_revision(revision_id const & new_id,
                    revision_data const & dat);
  
  void delete_existing_revs_and_certs();

  // crypto key / cert operations

  void get_key_ids(std::string const & pattern,
                   std::vector<rsa_keypair_id> & pubkeys,
                   std::vector<rsa_keypair_id> & privkeys);

  void get_private_keys(std::vector<rsa_keypair_id> & privkeys);

  bool key_exists(rsa_keypair_id const & id);

  bool public_key_exists(hexenc<id> const & hash);
  bool public_key_exists(rsa_keypair_id const & id);
  bool private_key_exists(rsa_keypair_id const & id);
  
  void get_pubkey(hexenc<id> const & hash, 
                  rsa_keypair_id & id,
                  base64<rsa_pub_key> & pub_encoded);

  void get_key(rsa_keypair_id const & id, 
               base64<rsa_pub_key> & pub_encoded);

  void get_key(rsa_keypair_id const & id, 
               base64< arc4<rsa_priv_key> > & priv_encoded);

  void put_key(rsa_keypair_id const & id, 
               base64<rsa_pub_key> const & pub_encoded);
  
  void put_key(rsa_keypair_id const & id, 
               base64< arc4<rsa_priv_key> > const & priv_encoded);
  
  void put_key_pair(rsa_keypair_id const & pub_id, 
                    base64<rsa_pub_key> const & pub_encoded,
                    base64< arc4<rsa_priv_key> > const & priv_encoded);

  void delete_private_key(rsa_keypair_id const & pub_id);

  // note: this section is ridiculous. please do something about it.

  bool manifest_cert_exists(manifest<cert> const & cert);
  bool manifest_cert_exists(hexenc<id> const & hash);
  void put_manifest_cert(manifest<cert> const & cert);

  bool revision_cert_exists(revision<cert> const & cert);
  bool revision_cert_exists(hexenc<id> const & hash);

  void put_revision_cert(revision<cert> const & cert);

  // this variant has to be rather coarse and fast, for netsync's use
  void get_revision_cert_index(std::vector< std::pair<hexenc<id>,
                               std::pair<revision_id, rsa_keypair_id> > > & idx);

  void get_revision_certs(cert_name const & name, 
                         std::vector< revision<cert> > & certs);

  void get_revision_certs(revision_id const & id, 
                         cert_name const & name, 
                         std::vector< revision<cert> > & certs);

  void get_revision_certs(cert_name const & name,
                         base64<cert_value> const & val, 
                         std::vector< revision<cert> > & certs);

  void get_revision_certs(revision_id const & id, 
                         cert_name const & name, 
                         base64<cert_value> const & value,
                         std::vector< revision<cert> > & certs);

  void get_revision_certs(revision_id const & id, 
                         std::vector< revision<cert> > & certs);

  void get_revision_cert(hexenc<id> const & hash,
                         revision<cert> & cert);
  
  void get_manifest_certs(manifest_id const & id, 
                          std::vector< manifest<cert> > & certs);

  void get_manifest_certs(cert_name const & name, 
                          std::vector< manifest<cert> > & certs);

  void get_manifest_certs(manifest_id const & id, 
                          cert_name const & name, 
                          std::vector< manifest<cert> > & certs);
  
  void get_manifest_cert(hexenc<id> const & hash,
                         manifest<cert> & cert);

  
  // merkle tree stuff

  bool merkle_node_exists(std::string const & type,
                          utf8 const & collection, 
                          size_t level,
                          hexenc<prefix> const & prefix);
  
  void get_merkle_node(std::string const & type,
                       utf8 const & collection, 
                       size_t level,
                       hexenc<prefix> const & prefix,
                       base64<merkle> & node);

  void put_merkle_node(std::string const & type,
                       utf8 const & collection, 
                       size_t level,
                       hexenc<prefix> const & prefix,
                       base64<merkle> const & node);

  void erase_merkle_nodes(std::string const & type,
                          utf8 const & collection);

  // completion stuff

  void complete(std::string const & partial,
                std::set<revision_id> & completions);

  void complete(std::string const & partial,
                std::set<manifest_id> & completions);
  
  void complete(std::string const & partial,
                std::set<file_id> & completions);

  void complete(commands::selector_type ty,
                std::string const & partial,
                std::vector<std::pair<commands::selector_type, 
                                      std::string> > const & limit,
                std::set<std::string> & completions);
  
  ~database();

};

// transaction guards nest. acquire one in any scope you'd like
// transaction-protected, and it'll make sure the db aborts a
// txn if there's any exception before you call commit()

class transaction_guard
{
  bool committed;
  database & db;
public:
  transaction_guard(database & d);
  ~transaction_guard();
  void commit();
};



#endif // __DATABASE_HH__
