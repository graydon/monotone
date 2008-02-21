
mtn_setup()

addfile("testfile", "foo bar")
check(mtn("ci", "-m", "foobar"), 0, false, false)
check(mtn("automate", "genkey", "foo@bar.com", "foopass"), 0, false, false)
check(mtn("automate", "genkey", "dbkey@bar.com", "foopass"), 0, false, false)
check(mtn("pubkey", "dbkey@bar.com"), 0, true)
rename("stdout", "dbkey")
check(mtn("dropkey", "dbkey@bar.com"), 0, false, false)
check(mtn("read"), 0, false, false, {"dbkey"})

-- foo@bar.com is now in keystore, dbkey@bar.com is in the DB

-- Should fail, foo@bar.com exists in the keystore
check(mtn("automate", "genkey", "foo@bar.com", "foopass"), 1, false, false)

-- Should fail, dbkey@bar.com exists in the DB
check(mtn("automate", "genkey", "dbkey@bar.com", "foopass"), 1, false, false)

-- Should fail, missing parameters
check(mtn("automate", "genkey", "bar@foo.com"), 1, false, false)

-- Should work, we'll check the output below
check(mtn("automate", "genkey", "foo@baz.com", "foopass"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))
locs = {}
for _,line in pairs(parsed) do
  if line.name == "name" then
    key = line.values[1]
    locs[key] = {db = false, ks = false, pub = false, priv = false}
  end
  if string.find(line.name, "location") then
    for _,v in pairs(line.values) do
      if v == "keystore" then locs[key].ks = true end
      if v == "database" then locs[key].db = true end
    end
  end
  if string.find(line.name, "private") then locs[key].priv = true end
  if string.find(line.name, "public") then locs[key].pub = true end
end
check(locs["foo@baz.com"].db == true)
check(locs["foo@baz.com"].ks == true)
check(locs["foo@baz.com"].priv == true)
check(locs["foo@baz.com"].pub == true)
