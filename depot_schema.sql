CREATE TABLE packets (
         major      INTEGER NOT NULL,
         minor      INTEGER NOT NULL,
         groupname  TEXT NOT NULL,
         adler32    TEXT NOT NULL,
         contents   TEXT NOT NULL,
         unique(groupname, contents),
         unique(major, minor, groupname)
         );
CREATE TABLE users (
         name     TEXT PRIMARY KEY,
         pubkey   TEXT NOT NULL
         );
