-- 
-- subsequent test for automate put_revision
-- this should error out since the file version for foo.txt is unknown
--
mtn_setup()

-- added files are checked
rev = "format_version \"1\"\n\nnew_manifest [0000000000000000000000000000000000000000]\n\nold_revision []\n\nadd_dir \"\"\n\nadd_file \"foo.txt\"\ncontent [1234567890123456789012345678901234567890]"
check(mtn("automate", "put_revision", rev), 3, false, false)

addfile("foo", "asdf")
commit()
fhash = sha1("foo")
rhash = base_revision()

-- modified files are also checked
rev = "format_version \"1\"\n\nnew_manifest [0000000000000000000000000000000000000000]\n\nold_revision [" .. rhash .. "]]\n\npatch \"foo\" from [" .. fhash .. "] to [0000000000000000000000000000000000000000]"
check(mtn("automate", "put_revision", rev), 3, false, false)
