-- Test note_netsync_*_sent hooks
--
-- Similar to
-- ../exchanging_work_via_netsync,_with_notes/__driver__.lua, but here
-- we do "push" instead of "pull".

include("common/netsync.lua")
mtn_setup()
netsync.setup_with_notes()
revs = {}

remove("_MTN")
check(mtn2("setup", "--branch=testbranch"), 0, nil, nil)

function noterev()
  local t = {}
  t.f = sha1("testfile")
  t.rev = base_revision()
  t.man = base_manifest()
  t.date = certvalue(t.rev, "date")
  table.insert(revs, t)
end

function evaluate(correctfile, logfile)
  check(get(correctfile))
  local dat = readfile(correctfile)
  dat = string.gsub(dat, "REV1", revs[1].rev)
  dat = string.gsub(dat, "MAN1", revs[1].man)
  dat = string.gsub(dat, "FILE1", revs[1].f)
  dat = string.gsub(dat, "DATE1", revs[1].date)
  dat = string.gsub(dat, "REV2", revs[2].rev)
  dat = string.gsub(dat, "MAN2", revs[2].man)
  dat = string.gsub(dat, "FILE2", revs[2].f)
  dat = string.gsub(dat, "DATE2", revs[2].date)
  writefile(correctfile, dat)

  canonicalize(logfile)
  dat = readfile(logfile)
  dat = string.gsub(dat, "^%d+ ", "")
  dat = string.gsub(dat, "\n%d+ ", "\n")
  dat = string.gsub(dat, "\n[^\n]*remote_host[^\n]*\n", "\n")
  dat = string.gsub(dat, "\n[^\n]*bytes in/out[^\n]*\n", "\n")
  writefile(logfile, dat)

  check(samefile(logfile, correctfile))
end

-- Checking the effect of a new revisions
writefile("testfile", "version 0 of test file")
check(mtn2("add", "testfile"), 0, false, false)
commit("testbranch", "blah-blah", mtn2)
noterev()

writefile("testfile", "version 1 of test file")
commit("testbranch", "blah-blah", mtn2)
noterev()

netsync.push("testbranch")

evaluate("testnotes.test", "testnotes-client.log")

-- Checking the effect of a simple cert change
check(mtn2("tag", revs[1].rev, "testtag"), 0, false, false)

netsync.push("testbranch")

evaluate("testnotes2.test", "testnotes-client.log")

-- end of file
