
mtn_setup()

-- reverting files deeper in the directory tree with only some leading
-- components of their relative path specified

mkdir("abc") mkdir("abc/def") mkdir("abc/def/ghi")
writefile("abc/def/ghi/file", "deep deep snow")
check(mtn("add", "abc/def/ghi/file"), 0, false, false)
commit()
writefile("abc/def/ghi/file", "deep deep mud")
check(mtn("status"), 0, true)
check(qgrep("abc/def/ghi/file", "stdout"))
check(mtn("revert", "abc/def"), 0, false, false)
check(mtn("status"), 0, true)
check(not qgrep("abc/def/ghi/file", "stdout"))
