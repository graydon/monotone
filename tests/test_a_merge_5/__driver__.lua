
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

-- This seems to be a really nasty testcase somehow.
-- merge(1) gives incorrect output.
-- diff3(1) reports a conflict, and is incorrect.
-- xxdiff and kdiff3 merge cleanly, and, AFAICT, correctly.
-- monotone used to be identical to merge(1) here; now it gives a conflict.

-- <njs`> the LCS that diff(1) calculates actually does explain the double-insertion
-- <njs`> I hate line-merging.
-- <njs`> anyway, I don't think there actually is a unique right way to merge this :-/

-- Since there's apparently no right way, we accept the conflict as a pass.

check(get("parent"))
check(get("left"))
check(get("right"))
check(get("correct"))

copy("parent", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
parent = base_revision()

copy("left", "testfile")
commit()

revert_to(parent)

copy("right", "testfile")
commit()

check(mtn("--branch=testbranch", "merge"), 1, false, false)

-- check(mtn("update"), 0, false, false)
-- check(samefile("testfile", "correct"))
