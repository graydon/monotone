 
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


-- primary data structures concerned with storing and 
-- versionning state-of-tree configurations

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

CREATE TABLE revisions
	(
	id primary key,      -- SHA1(text of revision)
	manifest not null    -- joins with manifests.id or manifest_deltas.id
	unique(id, manifest)
	);

CREATE TABLE changeset_adds
	(
	parent not null,     -- joins with revisions.id
	child not null,      -- joins with revisions.id
	path not null,       -- path name
	data not null,       -- joins with files.id or file_deltas.id
	unique(parent, child, path, data)
	);

CREATE TABLE changeset_deletes
	(
	parent not null,     -- joins with revisions.id
	child not null,      -- joins with revisions.id
	path not null,       -- path name
	unique(parent, child, path)
	);

CREATE TABLE changeset_renames
	(
	parent not null,     -- joins with revisions.id
	child not null,      -- joins with revisions.id
	src not null,        -- path name
	dst not null,        -- path name
	unique(parent, child, src, dst)
	);

CREATE TABLE changeset_deltas
	(
	parent not null,     -- joins with revisions.id
	child not null,      -- joins with revisions.id
	path not null,       -- path name
	src not null,        -- joins with files.id or file_deltas.id
	dst not null,        -- joins with files.id or file_deltas.id
	unique(parent, child, path, src, dst)
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

-- merkle nodes

CREATE TABLE merkle_nodes
	(
	type not null,                -- "key", "mcert", "fcert", "ccert"
	collection not null,          -- name chosen by user
	level not null,               -- tree level this prefix encodes
	prefix not null,              -- label identifying node in tree
	body not null,                -- binary, base64'ed node contents
	unique(type, collection, level, prefix)
	);

-- indices

CREATE INDEX revision_manifests ON revisions (manifest);

CREATE INDEX changeset_add_parents ON changeset_adds (parent);
CREATE INDEX changeset_add_childs ON changeset_adds (child);
CREATE INDEX changeset_add_paths ON changeset_adds (path);
CREATE INDEX changeset_add_data ON changeset_adds (data);

CREATE INDEX changeset_delete_parents ON changeset_deletes (parent);
CREATE INDEX changeset_delete_childs ON changeset_deletes (child);
CREATE INDEX changeset_delete_paths ON changeset_deletes (path);

CREATE INDEX changeset_rename_parents ON changeset_renames (parent);
CREATE INDEX changeset_rename_childs ON changeset_renames (child);
CREATE INDEX changeset_rename_srcs ON changeset_renames (src);
CREATE INDEX changeset_rename_dsts ON changeset_renames (dst);

CREATE INDEX changeset_delta_parents ON changeset_deltas (parent);
CREATE INDEX changeset_delta_childs ON changeset_deltas (child);
CREATE INDEX changeset_delta_paths ON changeset_deltas (path);
CREATE INDEX changeset_delta_srcs ON changeset_deltas (src);
CREATE INDEX changeset_delta_dsts ON changeset_deltas (dst);

-- views

CREATE VIEW revision_ancestry AS
	SELECT parent, child FROM changeset_adds
	UNION
	SELECT parent, child FROM changeset_deletes
	UNION
	SELECT parent, child FROM changeset_renames
	UNION
	SELECT parent, child FROM changeset_deltas
	;

CREATE VIEW path_ancestry AS
	SELECT parent, child, path FROM changeset_adds
	UNION
	SELECT parent, child, path FROM changeset_deletes
	UNION
	SELECT parent, child, src FROM changeset_renames
	UNION
	SELECT parent, child, dst FROM changeset_renames
	UNION
	SELECT parent, child, path FROM changeset_deltas
	;

CREATE VIEW revision_certs_with_trust AS
	SELECT 
		id, name, value, 
		trusted("revision", hash, id, name, value, keypair, signature) as trust
	FROM revision_certs
	GROUP BY id, name, value
	;

CREATE VIEW trusted_revision_certs AS
	SELECT id, name, value 
	FROM revision_certs_with_trust
	WHERE trust == 1
	;

CREATE VIEW manifest_certs_with_trust AS
	SELECT 
		id, name, value, 
		trusted("manifest", hash, id, name, value, keypair, signature) as trust
	FROM manifest_certs
	GROUP BY id, name, value
	;

CREATE VIEW trusted_manifest_certs AS
	SELECT id, name, value 
	FROM manifest_certs_with_trust
	WHERE trust == 1
	;

CREATE VIEW file_certs_with_trust AS
	SELECT 
		id, name, value, 
		trusted("file", hash, id, name, value, keypair, signature) as trust
	FROM file_certs
	GROUP BY id, name, value
	;

CREATE VIEW trusted_file_certs AS
	SELECT id, name, value 
	FROM file_certs_with_trust
	WHERE trust == 1
	;

CREATE VIEW trusted_parents_in_branch AS
	SELECT id, value
	FROM trusted_revision_certs
	WHERE name = "branch" AND id IN
		(SELECT parent FROM revision_ancestry)
	;

CREATE VIEW trusted_children_in_branch AS
	SELECT id, value
	FROM trusted_revision_certs
	WHERE name = "branch" AND id IN
		(SELECT child FROM revision_ancestry)
	;

CREATE VIEW branch_heads AS
	SELECT id, value FROM trusted_children_in_branch
	EXCEPT
	SELECT id, value FROM trusted_parents_in_branch
	;
