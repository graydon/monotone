
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("testfile", "blah stuff")
commit()
ver0 = base_revision()

addfile("testfile2", "some more data")
commit("otherbranch")
ver1 = base_revision()

netsync.pull("otherbranch")

check_same_stdout(mtn("ls", "certs", ver1),
                  mtn2("ls", "certs", ver1))
