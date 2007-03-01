mtn_setup()

contents = "blah\n"
contents2 = "blah\nblub\n"

writefile("expected", contents)
writefile("expected2", contents2)

-- check we error on wrong number of args
check(mtn("automate", "put_file"), 1, false, false)
check(mtn("automate", "put_file", "a", "b", "c"), 1, false, false)

-- check actually putting a file in
check(mtn("automate", "put_file", contents), 0, true, false)
canonicalize("stdout")
file = "4cbd040533a2f43fc6691d773d510cda70f4126a"
result = readfile("stdout")
check(result == file.."\n")

-- check that the file is there
check(mtn("automate", "get_file", file), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

check(mtn("automate", "put_file", file, contents2), 0, true, false)
canonicalize("stdout")
file2 = "ea2e27149f06a6519aa46084da815265c10b0a2a"
result = readfile("stdout")
check(result == file2.."\n")

-- check that the file is there
check(mtn("automate", "get_file", file2), 0, true, false)
canonicalize("stdout")
check(samefile("expected2", "stdout"))

-- check that nothing blows up if we feed it a file for a second time (with or
-- without a delta base)
check(mtn("automate", "put_file", contents2), 0, true, false)
canonicalize("stdout")
file2 = "ea2e27149f06a6519aa46084da815265c10b0a2a"
result = readfile("stdout")
check(result == file2.."\n")

check(mtn("automate", "put_file", file, contents2), 0, true, false)
canonicalize("stdout")
file2 = "ea2e27149f06a6519aa46084da815265c10b0a2a"
result = readfile("stdout")
check(result == file2.."\n")

-- check that things _do_ blow up if we feed it a file with a non-existent
-- base (even if we already have the file we are trying to write!)
check(mtn("automate", "put_file", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", contents2), 1, true, false)
