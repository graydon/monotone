
--
-- this is a bug report for a weird behaviour with revert
-- when a dropped (previously renamed) node should be reverted
-- and the rename has happened on one of the parents of the
-- node
-- see http://colabti.de/irclogger/irclogger_log/monotone?date=2008-01-18,Fri&sel=92#l174
-- for more details
--

mtn_setup()

check(mtn("mkdir", "old_dir"), 0, false, false)
writefile("old_dir/a", "blabla")
addfile("old_dir/a")
commit()

check(mtn("mv", "old_dir", "new_dir"), 0, false, false)
check(mtn("drop", "new_dir/a"), 0, false, false)

-- this is the actual command that misbehaves, in the way
-- that it does not re-create a under new_dir/a, but
-- old_dir/a, which fails, because old_dir is an old node
-- and renamed
check(mtn("revert", "old_dir/a"), 0, false, false)
xfail(mtn("status"), 0, false, false)
