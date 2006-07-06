
mtn_setup()

check(mtn("db", "migrate"), 0, false, false)

check(mtn("db", "execute", 'CREATE TABLE foo (bar primary key, baz not null)'), 0, false, false)

check(mtn("db", "migrate"), 1, false, false)
