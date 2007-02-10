
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

revs = {}

writefile("testfile", "foo bar")
check(mtn2("add", "testfile"), 0, false, false)
check(mtn2("commit", "--branch=testbranch", "--message=foo"), 0, false, false)
revs[1] = base_revision()

remove("_MTN")
check(mtn3("setup", "--branch=testbranch", "."), 0, false, false)
writefile("otherfile", "baz quux")
check(mtn3("add", "otherfile"), 0, false, false)
check(mtn3("commit", "--branch=testbranch", "--message=foo"), 0, false, false)
revs[2] = base_revision()

srv = netsync.start()

srv:sync("testbranch", 2)
srv:sync("testbranch", 3)

function chksy(n, co_mtn)
  check_same_stdout(mtn2("automate", "get_revision", revs[n]),
                    mtn3("automate", "get_revision", revs[n]))
  check_same_stdout(mtn2("ls", "certs", revs[n]),
                    mtn3("ls", "certs", revs[n]))
  remove("somedir")
  check(co_mtn("checkout", "--revision", revs[n], "somedir"), 0, false, false)
end

chksy(1, mtn3)

srv:sync("testbranch", 2)
chksy(2, mtn2)

-- And now make sure it works for children, where there are diffs and all

writefile("otherfile", "foo bar, baz, also quux (on off days)")
check(mtn3("commit", "--branch=testbranch", "--message=foo"), 0, false, false)
revs[3] = base_revision()

remove("_MTN")
remove("testfile")
check(mtn2("checkout", "--revision", revs[1], "."), 0, false, false)
writefile("testfile", "ptang")
check(mtn2("commit", "--branch=testbranch", "--message=foo"), 0, false, false)
revs[4] = base_revision()

-- And add a cert on an old revision
check(mtn3("comment", revs[1], 'sorry dave'), 0, false, false)

srv:sync("testbranch", 3)
srv:sync("testbranch", 2)

chksy(3, mtn2)

-- And check for that extra cert
check_same_stdout(mtn2("ls", "certs", revs[1]),
                  mtn3("ls", "certs", revs[1]))

srv:sync("testbranch", 3)

chksy(4, mtn3)

srv:finish()
