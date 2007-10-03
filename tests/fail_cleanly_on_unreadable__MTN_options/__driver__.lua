
skip_if(ostype == "Windows")
skip_if(string.sub(ostype, 1, 6) == "CYGWIN")
skip_if(not existsonpath("chmod"))
mtn_setup()

check({"chmod", "a-rwx", "_MTN/"})

function cleanup()
  check({"chmod", "u+rwx", "_MTN/"})
end

check(raw_mtn("status"), 1, false, false)
