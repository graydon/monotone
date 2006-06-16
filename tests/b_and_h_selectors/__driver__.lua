
include("common/selectors.lua")
mtn_setup()

addfile("testfile", "blah blah")
commit()
REV1=base_revision()

writefile("testfile", "stuff stuff")
commit()
REV2=base_revision()

writefile("testfile", "chew chew")
commit("otherbranch")
REV3=base_revision()

selmap("b:testbranch", {REV1, REV2})
selmap("b:otherbranch", {REV3})
selmap("b:", {REV3})
selmap("h:testbranch", {REV2})
selmap("h:otherbranch", {REV3})
selmap("h:", {REV3})
