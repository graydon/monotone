
mtn_setup()

-- check what happens if we give it no params (hint: it does not wait for stdin)
check(mtn("automate", "identify"), 1, false, false)

-- check if non-existing files are handled
check(mtn("automate", "identify", "non-existing-file"), 1, false, false)

-- check what happens if our filename is the stdin marker (hint: it should stop)
check(mtn("automate", "identify", "-"), 1, false, false)

-- finally check the actual functionality
writefile("testfile", "This is a testfile with test content.\n");
testfile_id = "4339e3c947e0d5abc83aef850db5ad6687559ae1"

check(mtn("automate", "identify", "testfile"), 0, true, false)
canonicalize("stdout")
check(samelines("stdout", { testfile_id }));

-- ensure that it also gets properly encoded via stdio
check(mtn("automate", "stdio"), 0, true, false, "l8:identify8:testfilee")
canonicalize("stdout")
check(samelines("stdout", { "0:0:l:41:" .. testfile_id }))

