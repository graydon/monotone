#ifndef __DATABASE_HH__
#define __DATABASE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

struct sqlite;
struct cert;

#include <vector>

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
struct posting;

class database
{
  fs::path filename;
  string const schema;
  void check_schema();

  struct sqlite * __sql;
  struct sqlite * sql(bool init = false);
  int transaction_level;

  typedef vector< vector<string> > results;
  void execute(char const * query, ...);
  void fetch(results & res, 
	     int const want_cols, 
	     int const want_rows, 
	     char const * query, ...);

  bool exists(hexenc<id> const & ident, 
	      string const & table);
  bool delta_exists(hexenc<id> const & ident,
		    string const & table);
  bool delta_exists(hexenc<id> const & ident,
		    hexenc<id> const & base,
		    string const & table);

  int count(string const & table);

  void get(hexenc<id> const & new_id,
	   base64< gzip<data> > & dat,
	   string const & table);
  void get_delta(hexenc<id> const & ident,
		 hexenc<id> const & base,
		 base64< gzip<delta> > & del,
		 string const & table);
  void get_version(hexenc<id> const & id,
		   base64< gzip<data> > & dat,
		   string const & data_table,
		   string const & delta_table);
  
  void put(hexenc<id> const & new_id,
	   base64< gzip<data> > const & dat,
	   string const & table);
  void drop(hexenc<id> const & base,
	    string const & table);
  void put_delta(hexenc<id> const & id,
		 hexenc<id> const & base,
		 base64< gzip<delta> > const & del,
		 string const & table);
  void put_version(hexenc<id> const & old_id,
		   hexenc<id> const & new_id,
		   base64< gzip<delta> > const & del,
		   string const & data_table,
		   string const & delta_table);

  bool cert_exists(cert const & t,
		  string const & table);
  void put_cert(cert const & t, string const & table);  
  void results_to_certs(results const & res,
		       vector<cert> & certs);

  void get_certs(hexenc<id> const & id, 
		vector< cert > & certs,
		string const & table);  

  void get_certs(cert_name const & name, 	      
		vector< cert > & certs,
		string const & table);

  void get_certs(hexenc<id> const & id,
		cert_name const & name,
		vector< cert > & certs,
		string const & table);  

  void get_certs(hexenc<id> const & id,
		cert_name const & name,
		base64<cert_value> const & val, 
		vector< cert > & certs,
		string const & table);  

  void get_certs(cert_name const & name,
		base64<cert_value> const & val, 
		vector<cert> & certs,
		string const & table);

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

  unsigned long get_statistic(string const & query);
  void set_filename(fs::path const & file);
  void initialize();
  void dump(ostream &);
  void load(istream &);
  void info(ostream &);
  void version(ostream &);
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
  

  // crypto key / cert operations

  void get_key_ids(string const & pattern,
		   vector<rsa_keypair_id> & pubkeys,
		   vector<rsa_keypair_id> & privkeys);

  void get_private_keys(vector<rsa_keypair_id> & privkeys);

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

  bool manifest_cert_exists(manifest<cert> const & cert);

  void put_manifest_cert(manifest<cert> const & cert);

  void get_manifest_certs(cert_name const & name, 
			 vector< manifest<cert> > & certs);

  void get_manifest_certs(manifest_id const & id, 
			 cert_name const & name, 
			 vector< manifest<cert> > & certs);

  void get_manifest_certs(cert_name const & name,
			 base64<cert_value> const & val, 
			 vector< manifest<cert> > & certs);

  void get_manifest_certs(manifest_id const & id, 
			 cert_name const & name, 
			 base64<cert_value> const & value,
			 vector< manifest<cert> > & certs);

  void get_manifest_certs(manifest_id const & id, 
			 vector< manifest<cert> > & certs);

  
  bool file_cert_exists(file<cert> const & cert);

  void put_file_cert(file<cert> const & cert);

  void get_file_certs(file_id const & id, 
		     vector< file<cert> > & certs);

  void get_file_certs(cert_name const & name, 
		     vector< file<cert> > & ts);

  void get_file_certs(file_id const & id, 
		     cert_name const & name, 
		     vector< file<cert> > & ts);

  void get_file_certs(file_id const & id, 
		     cert_name const & name,
		     base64<cert_value> const & val, 
		     vector< file<cert> > & ts);

  void get_file_certs(cert_name const & name,
		     base64<cert_value> const & val, 
		     vector< file<cert> > & certs);

  // network stuff

  void get_queued_targets(vector< pair<url,group> > & targets);

  void get_queued_contents(url const & u, 
			   group const & g, 
			   vector<string> & contents);
  
  void get_sequences(url const & u, 
		     group const & g, 
		     unsigned long & maj, 
		     unsigned long & min);

  void get_all_known_sources(vector< pair<url,group> > & sources);

  void put_sequences(url const & u, 
		     group const & g, 
		     unsigned long maj, 
		     unsigned long min);
  
  void queue_posting(url const & u, group const & g, 
		     string const & contents);

  void delete_posting(url const & u, group const & g, 
		      string const & contents);


  bool manifest_exists_on_netserver (url const & u, 
				     group const & g,
				     manifest_id const & m);

  void note_manifest_on_netserver (url const & u, 
				   group const & g,
				   manifest_id const & m);

  // completion stuff

  void complete(string const & partial,
		set<manifest_id> & completions);
  
  void complete(string const & partial,
		set<file_id> & completions);
  
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
