
mtn_setup()
times = {}

writefile("file1", "file 1 version 1")
times[1] = mtime("file1")

check(mtn("add", "file1"), 0, false, false)
commit()

-- ensure file modification time changes are detected

sleep(2)
writefile("file1", "file 1 version 2")
times[2] = mtime("file1")
check(times[2] > times[1])

-- revert the file and ensure that its modification time changes

sleep(2)
check(mtn("revert", "file1"), 0, false, false)

times[3] = mtime("file1")
check(times[3] > times[2])

-- revert the file again and ensure that its modification time does NOT change


sleep(2)
check(mtn("revert", "file1"), 0, false, false)

check(mtime("file1") == times[3])
