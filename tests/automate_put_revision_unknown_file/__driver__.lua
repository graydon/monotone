-- 
-- subsequent test for automate put_revision
-- this should error out since the file version for foo.txt is unknown
--
mtn_setup()

-- added files are checked
rev = ("format_version \"1\"\n\n"..
       "new_manifest [0000000000000000000000000000000000000000]\n\n"..
       "old_revision []\n\n"..
       "add_dir \"\"\n\n"..
       "add_file \"foo.txt\"\n"..
       " content [1234567890123456789012345678901234567890]")
check(mtn("automate", "put_revision", rev), 1, false, false)

addfile("foo", "asdf")
commit()
fhash = sha1("foo")
rhash = base_revision()

-- modified files are also checked
rev = ("format_version \"1\"\n\n"..
       "new_manifest [0000000000000000000000000000000000000000]\n\n"..
       "old_revision [" .. rhash .. "]\n\n"..
       "patch \"foo\"\n"..
       " from [" .. fhash .. "]\n"..
       "   to [0000000000000000000000000000000000000000]")
check(mtn("automate", "put_revision", rev), 1, false, false)
