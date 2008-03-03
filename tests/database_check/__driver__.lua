
mtn_setup()

writefile("file1", "file 1")
writefile("file2", "file 2")
writefile("file3", "file 3")

writefile("fileX", "coopers")

writefile("fileY", "vitamin")

revs = {}
files = {}
rosters = {}

function dbex(...)
  check(mtn("db", "execute", string.format(unpack(arg))), 0, false, false)
end

check(mtn("add", "file1"), 0, false, false)
commit("test", "add file1")
revs[1] = base_revision()
check(raw_mtn("db", "execute", "select hex(id) from rosters"), 0, true, false)
check(tail("stdout", 1), 0, true)
rosters[1] = trim(readfile("stdout"))

check(mtn("add", "file2"), 0, false, false)
commit("test", "add file2")
revs[2] = base_revision()
files[2] = sha1("file2")

check(mtn("add", "file3"), 0, false, false)
commit("test", "add file3")
files[3] = sha1("file3")
revs[3] = base_revision()

check(mtn("db", "check", "--ticker=dot"), 0, false, true)
check(qgrep('database is good', "stderr"))

-- remove file2 from the database invalidating roster2 and roster3
-- both of which include this file

dbex("delete from files where id=x'%s'", files[2])

check(mtn("db", "check", "--ticker=dot"), 1, false, true)
check(not qgrep('database is good', "stderr"))
check(qgrep('problems detected', "stderr"))
check(qgrep('1 missing file', "stderr"))
check(qgrep('2 incomplete roster', "stderr"))
check(qgrep('2 incomplete revision', "stderr"))
check(qgrep('total problems detected: 5', "stderr"))
check(qgrep('5 serious', "stderr"))


-- add an unreferenced file
copy("fileX", "stdin")
check(mtn("fload"), 0, false, false, true)
-- create an unreferenced roster by deleting the revision. Note that this will increment
-- the "missing revision" count by one for further checks
check(mtn("add", "fileY"), 0, false, false)
commit("test", "to be removed")
revs[4] = base_revision()
copy("_MTN/revision", "saved_revision")
dbex("delete from revisions where id=x'%s'", revs[4])
-- revert to the old workspace state
copy("saved_revision", "_MTN/revision")
-- remove another file too
dbex("delete from files where id=x'%s'", files[3])

check(mtn("db", "check", "--ticker=dot"), 1, false, true)
check(qgrep('2 unreferenced file', "stderr"))
check(qgrep('1 unreferenced roster', "stderr"))
check(qgrep('2 missing files', "stderr"))

dbex("delete from revision_ancestry")
check(mtn("db", "check", "--ticker=dot"), 1, false, true)
check(qgrep('2 mismatched parent', "stderr"))
check(qgrep('2 mismatched child', "stderr"))

-- bogus revision ancestry entries

xdelta_cc = "877cfe29db0f60dec63439857fe78673b9d55346"
xdelta_hh = "68d15dc01398c7bb375b1a90fbb420bebef1bac7"

dbex("insert into revision_ancestry values(x'%s', x'%s')", xdelta_cc, xdelta_hh)
check(mtn("db", "check", "--ticker=dot"), 1, false, true)
check(qgrep('3 mismatched parent', "stderr"))
check(qgrep('3 mismatched child', "stderr"))
check(qgrep('3 missing revision', "stderr"))

dbex("delete from roster_deltas where id=x'%s'", rosters[1])
check(mtn("db", "check", "--ticker=dot"), 1, false, true)
-- ROSTER TODO: need check_sane_history equivalent in db check
--check(grep '3 revisions with bad history' stderr, 0, false, false)

dbex("delete from revisions where id=x'%s'", revs[1])
check(mtn("db", "check", "--ticker=dot"), 1, false, true)
check(qgrep('4 missing revision', "stderr"))
-- ROSTER TODO
--check(grep '2 revisions with bad history' stderr, 0, false, false)

writefile("tosum", revs[2]..":comment:this is a test:tester@test.net:bogus sig")
hash = sha1("tosum")

dbex("insert into revision_certs values (x'%s', x'%s', 'comment', 'this is a test', 'tester@test.net', 'bogus sig')", hash, revs[2])
check(mtn("db", "check", "--ticker=dot"), 1, false, true)
check(qgrep('1 bad sig', "stderr"))

dbex("delete from revision_certs where name = 'date'")
check(mtn("db", "check", "--ticker=dot"), 1, false, true)
check(qgrep('2 missing certs', "stderr"))
check(qgrep('4 mismatched certs', "stderr"))

dbex("delete from public_keys")
check(mtn("db", "check", "--ticker=dot"), 1, false, true)
check(qgrep('1 missing key', "stderr"))
check(qgrep('13 unchecked signatures', "stderr"))
