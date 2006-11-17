
include("common/netsync.lua")
mtn_setup()
netsync.setup_with_notes()
revs = {}

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
check(mtn("add", "testfile"), 0, false, false)
commit()
noterev()

writefile("testfile", "version 1 of test file")
check(mtn("commit", "--message", "blah-blah"), 0, false, false)
noterev()

netsync.pull("testbranch")

evaluate("testnotes.test", "testnotes-client.log")

-- Checking the effect of a simple cert change
check(mtn("tag", revs[1].rev, "testtag"), 0, false, false)

netsync.pull("testbranch")

evaluate("testnotes2.test", "testnotes-client.log")

-- Checking that a netsync with nothing new will not trigger the
-- note_netsync hooks
-- remove("testnotes.log")
-- remove("testnotes.test")
-- netsync.pull("testbranch")

-- check(not exists("testnotes.log"))
