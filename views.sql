-- view-schema for the sql database. this file is converted into a
-- string constant, as the symbol:
-- 
-- char const views_constant[...] = { ... };
--
-- and emitted as views.h at compile time. it is used by
-- database.cc when opening an sqlite db.
--
-- views have no structural affect on the database, so we delete and
-- rebuild them every time we open the db (along with adding the
-- functions we're interested in). the schema migration is thus 
-- never concerned with changes to views.

CREATE VIEW manifest_certs_with_trust AS
	SELECT 
		mc.id as id, mc.name as name, mc.value as value, 
		trusted("manifest", mc.hash, mc.id, mc.name, 
	                mc.value, mc.keypair, pk.keydata, mc.signature) as trust
	FROM manifest_certs AS mc,
	     public_keys as pk
	WHERE pk.id == mc.keypair
	GROUP BY mc.id, mc.name, mc.value
	;

CREATE VIEW trusted_manifest_certs AS
	SELECT id, name, value 
	FROM manifest_certs_with_trust
	WHERE trust == 1
	;

CREATE VIEW revision_certs_with_trust AS
	SELECT 
		rc.id as id, rc.name as name, rc.value as value, 
		trusted("revision", rc.hash, rc.id, rc.name, 
	                rc.value, rc.keypair, pk.keydata, rc.signature) as trust
	FROM revision_certs AS rc,
	     public_keys as pk
	WHERE pk.id == rc.keypair
	GROUP BY rc.id, rc.name, rc.value
	;

CREATE VIEW trusted_revision_certs AS
	SELECT id, name, value 
	FROM revision_certs_with_trust
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
