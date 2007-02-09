mtn_setup()

addfile("base", "base")
commit()
base = base_revision()

addfile("next", "next")
commit()

check(mtn("checkout", "--revision", base, "test"))

mkdir("test/_MTN/detached")
check(indir("test", mtn("update")), 1, false, true)

writefile("test/_MTN/detached/123")
check(indir("test", mtn("update")), 1, false, true)

remove("test/_MTN/detached")
check(indir("test", mtn("update")), 0, false, true)

check(not exists("test/_MTN/detached"))
