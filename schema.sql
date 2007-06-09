 
-- schema for the sql database. this file is converted into
-- a string constant, as the symbol:
-- 
-- char const schema_constant[...] = { ... };
--
-- and emitted as schema.h at compile time. it is used by
-- database.cc when initializing a fresh sqlite db.


-- copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
-- all rights reserved.
-- licensed to the public under the terms of the GNU GPL 2.1+
-- see the file COPYING for details

PRAGMA page_size=8192;

-- Transactions avoid syncing for each action, db init gets faster.
BEGIN EXCLUSIVE;

-- primary data structures concerned with storing and 
-- versionning state-of-tree configurations

CREATE TABLE files
	(
	id primary key,   -- strong hash of file contents
	data not null     -- compressed contents of a file
	);

CREATE TABLE file_deltas
	(
	id not null,      -- strong hash of file contents
	base not null,    -- joins with files.id or file_deltas.id
	delta not null,   -- compressed rdiff to construct current from base
	unique(id, base)
	);

CREATE TABLE revisions
	(
	id primary key,      -- SHA1(text of revision)
	data not null        -- compressed, encoded contents of a revision
	);

CREATE TABLE revision_ancestry
	(
	parent not null,     -- joins with revisions.id
	child not null,      -- joins with revisions.id
	unique(parent, child)
	);

CREATE INDEX revision_ancestry__child ON revision_ancestry (child);

CREATE TABLE heights
	(
	revision not null,	-- joins with revisions.id
	height not null,	-- complex height, array of big endian u32 integers
	unique(revision, height)
	);
	
CREATE INDEX heights__height ON heights (height);

CREATE TABLE rosters
	(
	id primary key,         -- a revision id
	checksum not null,      -- checksum of 'data', to protect against disk corruption
	data not null           -- compressed, encoded contents of the roster
	);

CREATE TABLE roster_deltas
	(
	id primary key,         -- a revision id
	checksum not null,      -- checksum of 'delta', to protect against disk corruption
	base not null,          -- joins with either rosters.id or roster_deltas.id
	delta not null          -- rdiff to construct current from base
	);

CREATE TABLE next_roster_node_number
	(
	node primary key        -- only one entry in this table, ever
	);

-- structures for managing RSA keys and file / revision certs
 
CREATE TABLE public_keys
	(
	hash not null unique,   -- hash of remaining fields separated by ":"
	id primary key,         -- key identifier chosen by user
	keydata not null        -- RSA public params
	);

CREATE TABLE revision_certs
	(
	hash not null unique,   -- hash of remaining fields separated by ":"
	id not null,            -- joins with revisions.id
	name not null,          -- opaque string chosen by user
	value not null,         -- opaque blob
	keypair not null,       -- joins with public_keys.id
	signature not null,     -- RSA/SHA1 signature of "[name@id:val]"
	unique(name, id, value, keypair, signature)
	);

CREATE INDEX revision_certs__id ON revision_certs (id);
CREATE INDEX revision_certs__name_value ON revision_certs (name, value);

CREATE TABLE branch_epochs
	(
	hash not null unique,         -- hash of remaining fields separated by ":"
	branch not null unique,       -- joins with revision_certs.value
	epoch not null                -- random hex-encoded id
	);

-- database-local variables used to manage various things

CREATE TABLE db_vars
        (
        domain not null,      -- scope of application of a var
        name not null,        -- var key
        value not null,       -- var value
        unique(domain, name)
        );

-- obsolete tables kept around only to enable migration from
-- pre-roster monotone; manifest_certs may contain data of historical
-- interest in a db that was migrated from the changeset era, the
-- others should always be empty.

CREATE TABLE manifests
	(
	id primary key,      -- strong hash of all the entries in a manifest
	data not null        -- compressed, encoded contents of a manifest
	);

CREATE TABLE manifest_deltas
	(
	id not null,      -- strong hash of all the entries in a manifest
	base not null,    -- joins with either manifest.id or manifest_deltas.id
	delta not null,   -- rdiff to construct current from base
	unique(id, base)
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

COMMIT;
