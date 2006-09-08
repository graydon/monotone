
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

revs = {}

addfile("testfile", "blah stuff")
commit()
revs[0] = base_revision()

check(mtn("db", "execute", 'delete from revision_certs'), 0, false, false)

writefile("testfile", "other stuff")
commit()
revs[1] = base_revision()

netsync.pull("testbranch")

for _,x in pairs(revs) do
  check_same_stdout(mtn("automate", "get_revision", x),
                    mtn2("automate", "get_revision", x))
  check_same_stdout(mtn("ls", "certs", x),
                    mtn2("ls", "certs", x))
end
