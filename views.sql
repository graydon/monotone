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

CREATE VIEW trusted_branch_members AS 
        SELECT id, value FROM trusted_revision_certs
        WHERE name = "branch"
        ;

-- (id, value) in this table means that id is the parent of some child
-- that is in branch 'value'.  It does not mean anything about the
-- branch that 'id' is in!
CREATE VIEW trusted_branch_parents AS
        SELECT parent, value FROM trusted_branch_members, revision_ancestry
        WHERE child = id
        ;

-- Because sqlite 2 does not support naming the columns in a view,
-- this view has columns (parent, value).  This is confusing and
-- should be fixed when sqlite starts supporting 'CREATE VIEW name (columns)'
-- syntax.
CREATE VIEW branch_heads AS
        SELECT id, value FROM trusted_branch_members
        EXCEPT
        SELECT parent, value FROM trusted_branch_parents
        ;
