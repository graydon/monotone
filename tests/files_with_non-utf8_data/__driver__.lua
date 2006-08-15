
mtn_setup()

check(get("ru-8859-5"))
check(get("ru-utf8"))
check(get("jp-euc"))
check(get("jp-utf8"))

check(get("8859-5.lua"))
check(get("euc.lua"))

-- Create a mock base revision to revert back to
addfile("blah", "foo foo")
commit()
base = base_revision()

-- Test 8859-5 (Russian) conversions.

check(mtn("--rcfile=8859-5.lua", "add", "ru-8859-5"), 0, false, false)
check(mtn("--rcfile=8859-5.lua", "commit", "--message=foo"), 0, false, false)
ru = base_revision()
check(mtn("--rcfile=8859-5.lua", "checkout", "--revision", ru, "co-ru-8859-5"), 0, false, false)
check(samefile("ru-8859-5", "co-ru-8859-5/ru-8859-5"))
check(mtn("checkout", "--revision", ru, "co-ru-utf8"), 0, false, false)
check(samefile("ru-utf8", "co-ru-utf8/ru-8859-5"))

revert_to(base)

-- Test EUC-JP (Japanese) conversions.

check(mtn("--rcfile=euc.lua", "add", "jp-euc"), 0, false, false)
check(mtn("--rcfile=euc.lua", "commit", "--message=foo"), 0, false, false)
jp = base_revision()
check(mtn("--rcfile=euc.lua", "checkout", "--revision", jp, "co-jp-euc"), 0, false, false)
check(samefile("jp-euc", "co-jp-euc/jp-euc"))
check(mtn("checkout", "--revision", jp, "co-jp-utf8"), 0, false, false)
check(samefile("jp-utf8", "co-jp-utf8/jp-euc"))
