
mtn_setup()

addfile("zoo", "blah\n")
check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false) 
          
-- ensure that bad restriction paths fail
check(mtn("automate", "get_current_revision", "foo-bar"), 1, true, false)
check(fsize("stdout") == 0)

addfile("foox", "blah\n")
addfile("barx", "blah2\n")

-- ensure that no restriction yields the same as '.' as restriction
check(mtn("automate", "get_current_revision"), 0, true, false)
no_restrict = get("stdout")

check(mtn("automate", "get_current_revision", "."), 0, true, false)
with_restrict = get("stdout")
check( no_restrict == with_restrict)

check(mtn("automate", "get_current_revision", "foox"), 0, true, false)
foo_restrict = get("stdout")
check(     qgrep("foox", "stdout") )
check( not qgrep("barx", "stdout") )
check( not qgrep("zoo", "stdout") )


check(mtn("automate", "get_current_revision", "barx"), 0, true, false)
check(     qgrep("barx", "stdout") )
check( not qgrep("foox", "stdout") )
check( not qgrep("zoo", "stdout") )

-- check subdirectory restrictions
mkdir("ttt")
mkdir("ttt/yyy")
mkdir("ttt/xxx")

addfile("ttt/yyy/zzz", "blah\n")
addfile("ttt/xxx/vvv", "blah\n")

check(mtn("automate", "get_current_revision", "ttt/"), 0, true, false)
check(     qgrep("ttt", "stdout") )
check(     qgrep("zzz", "stdout") )
check(     qgrep("vvv", "stdout") )
check( not qgrep("foox", "stdout") )
check( not qgrep("barx", "stdout") )

check(mtn("automate", "get_current_revision", "--depth=0", "ttt", "ttt/xxx", "ttt/xxx/vvv"), 0, true, false)
check(     qgrep("ttt/xxx/vvv", "stdout") )
-- XXX: check node_restriction for 
--      looks like yyy gets into revision even if we explicitly forbid recursion
-- check( not qgrep("yyy", "stdout") )
check( not qgrep("zzz", "stdout") )
check( not qgrep("foox", "stdout") )
check( not qgrep("barx", "stdout") )

