#ifndef __DATABASE_HH__
#define __DATABASE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

struct sqlite;
struct cert;

#include <vector>
#include <set>
#include <string>

#include <boost/filesystem/path.hpp>

#include "manifest.hh"
#include "vocab.hh"

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
class reverse_queue;
struct posting;

class database
{
  fs::path filename;
  std::string const schema;
  void check_schema();

  struct sqlite * __sql;
  struct sqlite * sql(bool init = false);
  int transaction_level;

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
  friend class reverse_queue;
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
  void ensure_open();
  
  bool file_version_exists(file_id const & id);
  bool manifest_version_exists(manifest_id const & id);
  
  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist)
  void get_file_version(file_id const & id,
			file_data & dat);

  // put file w/o predecessor into db
  void put_file(file_id const & new_id,
		file_data const & dat);

  // store new version and update old version to be a delta
  void put_file_version(file_id const & old_id,
			file_id const & new_id,
			file_delta const & del);

  // get plain version if it exists, or reconstruct version
  // from deltas (if they exist). 
  void get_manifest_version(manifest_id const & id,
			    manifest_data & dat);

  // put manifest w/o predecessor into db
  void put_manifest(manifest_id const & new_id,
		    manifest_data const & dat);

  // store new version and update old version to be a delta
  void put_manifest_version(manifest_id const & old_id,
			    manifest_id const & new_id,
			    manifest_delta const & del);


  // only use these three variants if you really know what you're doing,
  // wrt. "old" and "new". they will throw if you do something wrong.

  bool manifest_delta_exists(manifest_id const & new_id,
			     manifest_id const & old_id);

  void compute_older_version(manifest_id const & new_id,
			     manifest_id const & old_id,
			     data const & m_new,
			     data & m_old);

  void compute_older_version(manifest_id const & new_id,
			     manifest_id const & old_id,
			     manifest_data const & m_new,
			     manifest_data & m_old);
  

  // crypto key / cert operations

  void get_key_ids(std::string const & pattern,
		   std::vector<rsa_keypair_id> & pubkeys,
		   std::vector<rsa_keypair_id> & privkeys);

  void get_private_keys(std::vector<rsa_keypair_id> & privkeys);

  bool key_exists(rsa_keypair_id const & id);
  bool public_key_exists(rsa_keypair_id const & id);
  bool private_key_exists(rsa_keypair_id const & id);
  
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

  // note: this section is ridiculous. please do something about it.

  void get_head_candidates(std::string const & branch_encoded,
			   std::vector< manifest<cert> > & branch_certs,
			   std::vector< manifest<cert> > & ancestry_certs);

  bool manifest_cert_exists(manifest<cert> const & cert);

  void put_manifest_cert(manifest<cert> const & cert);

  void get_manifest_certs(cert_name const & name, 
			 std::vector< manifest<cert> > & certs);

  void get_manifest_certs(manifest_id const & id, 
			 cert_name const & name, 
			 std::vector< manifest<cert> > & certs);

  void get_manifest_certs(cert_name const & name,
			 base64<cert_value> const & val, 
			 std::vector< manifest<cert> > & certs);

  void get_manifest_certs(manifest_id const & id, 
			 cert_name const & name, 
			 base64<cert_value> const & value,
			 std::vector< manifest<cert> > & certs);

  void get_manifest_certs(manifest_id const & id, 
			 std::vector< manifest<cert> > & certs);

  
  bool file_cert_exists(file<cert> const & cert);

  void put_file_cert(file<cert> const & cert);

  void get_file_certs(file_id const & id, 
		     std::vector< file<cert> > & certs);

  void get_file_certs(cert_name const & name, 
		     std::vector< file<cert> > & ts);

  void get_file_certs(file_id const & id, 
		     cert_name const & name, 
		     std::vector< file<cert> > & ts);

  void get_file_certs(file_id const & id, 
		     cert_name const & name,
		     base64<cert_value> const & val, 
		     std::vector< file<cert> > & ts);

  void get_file_certs(cert_name const & name,
		     base64<cert_value> const & val, 
		     std::vector< file<cert> > & certs);

  // network stuff

  void get_queued_targets(std::set<url> & targets);

  void get_queue_count(url const & u, size_t & num_packets);

  void get_queued_content(url const & u, 
			  size_t const & queue_pos,
			  std::string & content);
  
  void get_sequences(url const & u, 
		     unsigned long & maj, 
		     unsigned long & min);

  void get_all_known_sources(std::set<url> & sources);

  void put_sequences(url const & u, 
		     unsigned long maj, 
		     unsigned long min);
  
  void queue_posting(url const & u,
		     std::string const & contents);

  void delete_posting(url const & u,
		      size_t const & queue_pos);


  bool manifest_exists_on_netserver (url const & u, 
				     manifest_id const & m);

  void note_manifest_on_netserver (url const & u, 
				   manifest_id const & m);

  // completion stuff

  void complete(std::string const & partial,
		std::set<manifest_id> & completions);
  
  void complete(std::string const & partial,
		std::set<file_id> & completions);
  
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

// a reverse_queue is an object which creates a temporary table for
// postings, and then queues the postings (in reverse) to its database when
// it is destroyed, and deletes the table.

class reverse_queue
{
  database & db;
public:
  reverse_queue(database & d);
  void reverse_queue_posting(url const & u,
			     std::string const & contents);
  ~reverse_queue();
};


#endif // __DATABASE_HH__
