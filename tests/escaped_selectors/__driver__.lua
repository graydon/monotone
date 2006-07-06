
include("common/selectors.lua")
mtn_setup()

addfile("testfile", "blah blah")
commit("test/branch")
REV1=base_revision()

writefile("testfile", "stuff stuff")
commit("test/branch")
REV2=base_revision()

writefile("testfile", "chew chew")
commit("other/branch")
REV3=base_revision()

selmap("b:test\\/branch", {REV1, REV2})
selmap("b:other\\/branch", {REV3})
selmap("b:", {REV3})
selmap("h:test\\/branch", {REV2})
selmap("h:other\\/branch", {REV3})
selmap("h:", {REV3})
