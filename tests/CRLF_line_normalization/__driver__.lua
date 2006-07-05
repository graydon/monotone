
mtn_setup()

addfile("test.crlf", "a\r\nb\r\nc\r\nd\r\n")
commit()

writefile("test.crlf", "a\r\nb\r\nnew line!\r\nc\r\nd\r\n")

check(mtn("diff"), 0, true, false)
lines = 0
for i in io.lines("stdout") do lines = lines + 1 end
check(lines == 16) 
