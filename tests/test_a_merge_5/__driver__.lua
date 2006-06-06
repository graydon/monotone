
mtn_setup()

-- This test is a bug report.

-- Another real merge error. This is the diff between the correct
-- result and the file produced by the merge:
--
-- --- correct
-- +++ testfile
-- @@ -99,6 +99,8 @@
--  
--    void get_ids(std::string const & table, std::set< hexenc<id> > & ids); 
--  
-- +  void get_ids(std::string const & table, std::set< hexenc<id> > & ids); 
-- +
--    void get(hexenc<id> const & new_id,
--             base64< gzip<data> > & dat,
--             std::string const & table);

-- This seems to be a really nasty testcase somehow.  monotone and
-- merge(1) give identical, incorrect output.  diff3(1) reports a
-- conflict.  xxdiff merges cleanly, and, AFAICT, correctly.

getfile("parent")
getfile("left")
getfile("right")
getfile("correct")

copyfile("parent", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
parent = base_revision()

copyfile("left", "testfile")
commit()

revert_to(parent)

copyfile("right", "testfile")
commit()

check(mtn("--branch=testbranch", "merge"), 0, false, false)

check(mtn("update"), 0, false, false)
xfail_if(true, samefile("testfile", "correct"))
