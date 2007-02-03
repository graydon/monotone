
mtn_setup()

-- The patch for this security issue is to treat all case-folded
-- versions of _MTN as being bookkeeping files (and thus illegal
-- file_paths).  Make sure it's working.

names = {"_mtn", "_mtN", "_mTn", "_Mtn", "_MTn", "_MtN", "_mTN", "_MTN"}

-- bookkeeping files are an error for add
for _,i in pairs(names) do if not exists(i) then writefile(i) end end
for _,i in pairs(names) do
  check(mtn("add", i), 1, false, true)
  check(qgrep(i, "stderr"))
end
check(mtn("ls", "known"), 0, true, false)
check(grep("-qi", "_mtn", "stdout"), 1)

for _,i in pairs(names) do remove(i) end

-- run setup again, because we've removed our bookkeeping dir.
check(mtn("--branch=testbranch", "setup", "."))

-- files in bookkeeping dirs are also ignored by add
-- (mkdir -p used because the directories already exist on case-folding FSes)
for _,i in pairs(names) do
  if not exists(i) then mkdir(i) end
  writefile(i.."/foo", "")
end
for _,i in pairs(names) do
  check(mtn("add", i), 1, false, true)
  check(qgrep(i, "stderr"))
end
check(mtn("ls", "known"), 0, true, false)
check(grep("-qi", "_mtn", "stdout"), 1)

for _,i in pairs(names) do remove(i) end

-- just to make sure, check that it's not only add that fails, if it somehow
-- sneaks into an internal format then that fails too
remove("_MTN")
check(mtn("--branch=testbranch", "setup", "."))
mkdir("_mTn")
writefile("_MTN/revision",
             'format_version "1"\n'
          .. '\n'
          .. 'new_manifest []\n'
          .. '\n'
          .. 'old_revision []\n'
          .. '\n'
          .. 'add_dir ""\n'
          .. '\n'
          .. 'add_dir "_mTn"\n')
check(mtn("status"), 3, false, false)
check(mtn("commit", "-m", "blah"), 3, false, false)

-- assert trips if we have a db that already has a file with this sort
-- of name in it.  it would be better to test that checkout or pull fail, but
-- it is too difficult to regenerate this database every time things change,
-- and in fact we know that the same code paths are exercised by this.
for _,i in pairs({"files", "dirs"}) do
  get(i..".db.dumped", "stdin")
  check(mtn("db", "load", "-d", i..".mtn"), 0, false, false, true)
  check(mtn("db", "migrate", "-d", i..".mtn"), 0, false, false)
  check(mtn("-d", i..".mtn", "db", "regenerate_caches"), 3, false, false)
end
