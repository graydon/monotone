
include("common/selectors.lua")
mtn_setup()
revs = {}

addfile("testfile", "this is just a file")
copy("testfile", "testfile1")
check(mtn("commit", "--date=2005-03-11T20:33:01", "--branch=foo",
          "--message=march"), 0, false, false)
revs.first = base_revision()

writefile("testfile", "Now, this is a different file")
copy("testfile", "testfile2")
check(mtn("commit", "--date=2005-04-22T12:15:00", "--branch=foo",
          "--message=aprila"), 0, false, false)
revs.second = base_revision()

writefile("testfile", "And we change it a third time")
copy("testfile", "testfile3")
check(mtn("commit", "--date=2005-04-24T07:44:39", "--branch=foo",
          "--message=aprilb"), 0, false, false)
revs.third = base_revision()

-- -------------------
-- check 'earlier or equal' selector
-- -------------------

-- this time is just 'before' the first commit, thus no output should come
selmap("e:2005-03-11T20:33:00", {})

-- these sels should extract only the first commit
-- Note: the second sel is the exact time of the first commit.
selmap("e:2005-04", {revs.first})
selmap("e:2005-03-11T20:33:01", {revs.first})
selmap("e:2005-03-11T20:33:02", {revs.first})

-- now the first two
selmap("e:2005-04-23", {revs.first, revs.second})

-- finally, all the files
selmap("e:2005-04-30", {revs.first, revs.second, revs.third})
selmap("e:2006-07", {revs.first, revs.second, revs.third})

-- -------------------
-- check 'later' selector
-- -------------------

-- unlike 'earlier', the 'later' selector matches only strictly greater
-- commit times.  Giving a time equal to that of third commit thus
-- should not match anything
selmap("l:2005-04-24T07:44:39", {})
selmap("l:2005-05", {})

-- these sels should extract only the last commit
-- Note: the second sel is one sec before the last commit
selmap("l:2005-04-23", {revs.third})
selmap("l:2005-04-24T07:44:38", {revs.third})

-- now we match the second and third commit
selmap("l:2005-04-21", {revs.third, revs.second})

-- finally, all the files
selmap("l:2005-03", {revs.first, revs.second, revs.third})
selmap("l:2003-01", {revs.first, revs.second, revs.third})

-- -------------------
-- check combined selectors
-- -------------------

-- matching only the second commit
selmap("l:2005-04-01/e:2005-04-23", {revs.second})
selmap("l:2005-04-01/e:2005-04-22T20:00:00", {revs.second})
selmap("l:2005-04-21T23:01:00/e:2005-04-22T20:00:00", {revs.second})

-- non overlapping intervals should not match, even if the single selector 
-- will
selmap("l:2005-04-22/e:2005-04-21", {})
