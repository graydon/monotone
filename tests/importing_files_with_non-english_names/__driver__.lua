
mtn_setup()

european_utf8 = "\195\182\195\164\195\188\195\159"
             -- "\xC3\xB6\xC3\xA4\xC3\xBc\xC3\x9F"
european_8859_1 = "\246\228\252\223"
               -- "\xF6\xE4\xFC\xDF"

japanese_utf8 = "\227\129\166\227\129\153\227\129\168"
             -- "\xE3\x81\xA6\xE3\x81\x99\xE3\x81\xA8"
japanese_euc_jp = "\164\198\164\185\164\200"
               -- "\xA4\xC6\xA4\xB9\xA4\xC8"

if ostype == "Windows" or string.sub(ostype, 1, 6) == "CYGWIN" then
  funny_filename = "file+name-with_funny@symbols%etc"
else
  funny_filename = "file+name-with_funny@symbols%etc:"
end

for _,name in pairs{"weird", "utf8", "8859-1", "euc"} do
  mkdir(name)
end
check(writefile("weird/file name with spaces", ""))
check(writefile("weird/" .. funny_filename, ""))
check(writefile("utf8/" .. european_utf8, ""))
check(writefile("utf8/" .. japanese_utf8, ""))

if ostype ~= "Darwin" then
	check(writefile("8859-1/" .. european_8859_1, ""))
	check(writefile("euc/" .. japanese_euc_jp, ""))
end

check(mtn("add", "weird/file name with spaces"), 0, false, false)
check(mtn("add", "weird/" .. funny_filename), 0, false, false)

-- add some files with UTF8 names
set_env("LANG", "en_US.utf-8")
set_env("CHARSET", "UTF-8")
check(mtn("add", "utf8/" .. european_utf8), 0, false, false)
check(mtn("add", "utf8/" .. japanese_utf8), 0, false, false)

commit()

-- check the names showed up in our manifest

set_env("LANG", "en_US.utf-8")
set_env("CHARSET", "UTF-8")

check(mtn("automate", "get_manifest_of"), 0, true)
rename("stdout", "manifest")
check(qgrep("funny", "manifest"))
check(qgrep("spaces", "manifest"))
check(qgrep(japanese_utf8, "manifest"))
check(qgrep(european_utf8, "manifest"))

-- okay, now we try in two different locales.  monotone is happy to
-- have arbirary utf8 filenames in it, but these locales don't support
-- arbitrary utf8 -- you have to use a utf8 locale if you want to put
-- filenames on your disk in utf8.  if we keep all the utf8 files in
-- the tree, then, monotone will attempt to convert them to the current
-- locale, and fail miserably.  so get rid of them first.

check(mtn("drop", "--bookkeep-only", "utf8/" .. european_utf8, "utf8/" .. japanese_utf8), 0, false, false)
commit()

-- OS X expects data passed to the OS to be utf8, so these tests don't make
-- sense.
if ostype ~= "Darwin" then
	-- now try iso-8859-1

	set_env("LANG", "de_DE.iso-8859-1")
	set_env("CHARSET", "iso-8859-1")
	check(mtn("add", "8859-1/" .. european_8859_1), 0, false, false)

	commit()
end

-- check the names showed up in our manifest

check(mtn("automate", "get_manifest_of"), 0, true)
rename("stdout", "manifest")
check(qgrep("funny", "manifest"))
check(qgrep("spaces", "manifest"))
if ostype ~= "Darwin" then
  check(qgrep("8859-1/" .. european_utf8, "manifest"))
end

-- okay, clean up again

if ostype ~= "Darwin" then
	check(mtn("drop", "--bookkeep-only", "8859-1/" .. european_8859_1), 0, false, false)
	commit()
end

-- now try euc

if ostype ~= "Darwin" then
	set_env("LANG", "ja_JP.euc-jp")
	set_env("CHARSET", "euc-jp")
	check(mtn("add", "euc/" .. japanese_euc_jp), 0, false, false)

	commit()
end

-- check the names showed up in our manifest

check(mtn("automate", "get_manifest_of"), 0, true)
rename("stdout", "manifest")
check(qgrep("funny", "manifest"))
check(qgrep("spaces", "manifest"))
if ostype ~= "Darwin" then
	check(qgrep("euc/" .. japanese_utf8, "manifest"))
end
