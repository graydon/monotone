 
-- schema for the sql database. this file is converted into
-- a string constant, as the symbol:
-- 
-- char const schema_constant[...] = { ... };
--
-- and emitted as schema.h at compile time. it is used by
-- database.cc when initializing a fresh sqlite db.


-- copyright (C) 2002 graydon hoare <graydon@pobox.com>
-- all rights reserved.
-- licensed to the public under the terms of the GNU GPL 2.1+
-- see the file COPYING for details


-- primary data structures concerned with storing and 
-- versionning state-of-tree configurations

CREATE TABLE schema_version
	(
	version primary key
	);

CREATE TABLE files
	(
	id primary key,   -- strong hash of file contents
	data not null     -- compressed, encoded contents of a file
	); 

CREATE TABLE file_deltas
	(	
	id not null,      -- strong hash of file contents
	base not null,    -- joins with files.id or file_deltas.id
	delta not null,   -- rdiff to construct current from base
	unique(id, base)
	);

CREATE TABLE manifests
	(
	id primary key,      -- strong hash of all the entries in a manifest
	data not null        -- compressed, encoded contents of a manifest
	);

CREATE TABLE manifest_deltas
	(
	id not null,         -- strong hash of all the entries in a manifest
	base not null,       -- joins with either manifest.id or manifest_deltas.id
	delta not null,      -- rdiff to construct current from base
	unique(id, base)
	);

-- structures for managing RSA keys and file / manifest certs
 
CREATE TABLE public_keys
	(
	hash not null unique,   -- hash of remaining fields separated by ":"
	id primary key,         -- key identifier chosen by user
	keydata not null        -- RSA public params
	);

CREATE TABLE private_keys
	(
	hash not null unique,   -- hash of remaining fields separated by ":"
	id primary key,         -- as in public_keys (same identifiers, in fact)
	keydata not null        -- encrypted RSA private params
	);

CREATE TABLE manifest_certs
	(
	hash not null unique,   -- hash of remaining fields separated by ":"
	id not null,            -- joins with manifests.id or manifest_deltas.id
	name not null,          -- opaque string chosen by user
	value not null,         -- opaque blob
	keypair not null,       -- joins with public_keys.id
	signature not null,     -- RSA/SHA1 signature of "[name@id:val]"
	unique(name, id, value, keypair, signature)
	);

CREATE TABLE file_certs
	(
	hash not null unique,   -- hash of remaining fields separated by ":"
	id not null,            -- joins with files.id or file_deltas.id
	name not null,          -- opaque string chosen by user
	value not null,         -- opaque blob
	keypair not null,       -- joins with public_keys.id
	signature not null,     -- RSA/SHA1 signature of "[name@id:val]"
	unique(name, id, value, keypair, signature)
	);

-- structures for managing our relationship to netnews or depots nb:
-- these are all essentially transient data, and are not represented
-- by a dump of the packets making up the database.

CREATE TABLE posting_queue
	(
	url not null,       -- URL we are going to send this to
	content not null    -- the packets we're going to send
	);

CREATE TABLE incoming_queue
	(
	url not null,       -- URL we got this bundle from
	content not null    -- the packets we're going to read
	);

CREATE TABLE sequence_numbers
	(
	url primary key,      -- URL to read from
	major not null,       -- 0 in news servers, may be higher in depots
	minor not null        -- last article / packet sequence number we got
	);

CREATE TABLE netserver_manifests
	(
	url not null,         -- url of some server
	manifest not null,    -- manifest which exists on url
	unique(url, manifest)
	);

-- merkle nodes

CREATE TABLE merkle_nodes
	(
	type not null,                -- "key", "mcert", "fcert", "manifest"
	collection not null,          -- name chosen by user
	level not null,               -- tree level this prefix encodes
	prefix not null,              -- label identifying node in tree
	body not null,                -- binary, base64'ed node contents
	unique(type, collection, level, prefix)
	);


