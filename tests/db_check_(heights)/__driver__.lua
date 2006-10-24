-- -*-lua-*-
mtn_setup()

-- make two revisions
addfile("testfile", "A")
commit()
revA = base_revision()
writefile("testfile", "B")
commit()
revB = base_revision()

-- db should be ok
check(mtn("db", "check"), 0, false, false)

-- swap the two heights (by swapping their revs)
check(mtn("db", "execute", "update heights set revision='temp' where revision='" .. revA .. "';"), 0, false, false)
check(mtn("db", "execute", "update heights set revision='".. revA .. "' where revision='" .. revB .. "';"), 0, false, false)
check(mtn("db", "execute", "update heights set revision='".. revB .. "' where revision='temp';"), 0, false, false)

-- check
check(mtn("db", "check"), 1, false, true)
check(qgrep('1 incorrect heights', 'stderr'))
check(qgrep(revB, 'stderr'))
check(qgrep('serious problems detected', 'stderr'))

-- delete one of the heights
check(mtn("db", "execute", "delete from heights where revision='" .. revA .. "';"), 0, false, false)

-- check again
check(mtn("db", "check"), 1, false, true)
check(qgrep('1 missing heights', 'stderr'))
check(qgrep(revA, 'stderr'))
check(qgrep('serious problems detected', 'stderr'))

-- duplicate the remaining height
check(mtn("db", "execute", "insert into heights (revision, height) values ('" .. revA .. "', (select height from heights where revision='" .. revB .. "'));"), 0, false, false)

-- check once more
check(mtn("db", "check"), 1, false, true)
check(qgrep('1 duplicate heights', 'stderr'))
--no check for the rev, because we don't know which one is reported first
check(qgrep('1 incorrect heights', 'stderr'))
check(qgrep(revB, 'stderr'))
check(qgrep('serious problems detected', 'stderr'))
