-- user interaction commands that produce a lot of output, like 'mtn
-- log', should die immediately on SIGPIPE (which they'll get if, for
-- instance, the user pipes the command to a pager, reads the first
-- few dozen lines, and then quits the pager).

skip_if(ostype=="Windows")
SIGPIPE = 13 -- what it is on traditional Unixy systems

-- Quick refresher on named pipe semantics: When initially created,
-- open() for read will block until another process open()s it for
-- write, and vice versa.  Once a named pipe has been opened once
-- for read and once for write, it is functionally identical to an
-- anonymous pipe.
--
-- The kernel allocates a finite amount of buffer space for each pipe.
-- An attempt to write more than that amount to the pipe will block
-- until at least some data has been read from the pipe.  (There are
-- complicated rules for exactly how this works, interacting with
-- O_NONBLOCK and an atomicity guarantee for small writes, but they do
-- not concern us here as we are using neither O_NONBLOCK nor multiple
-- concurrent writers to the pipe.)
--
-- A writer process receives SIGPIPE when it attempts to write() to a
-- pipe that has been opened for read and then closed.  However, if
-- the writer manages to fit all the data it has into the pipe's
-- kernel buffer, before the reader closes its end, SIGPIPE will not
-- be delivered.  Thus, we must arrange for the writer to emit so much
-- data that it fills the kernel buffer, and for this to happen before
-- the reader closes its end.  Unfortunately, there is no way to find
-- out how much buffer space the kernel provides, so we resort to
-- writing a ridiculous amount of data.  Typical pipe capacities are
-- 4K and 64K, so "ridiculous" in this case is "approximately 256K."
--
-- We would like to make that data realistic -- typical commits to the
-- monotone repository have between 100 and 200 characters of commit
-- log -- but in order to get the ridiculous 256K, we would need
-- several thousand commits, which would take too long to execute.
-- So instead we make four commits each with 64K commit messages.
-- (There is a temptation to make it sixteen; I can imagine someone
-- deciding to bulk up the pipe buffer to a nice round megabyte.
-- But the time this test takes is proportional to the number of
-- commits, so I won't.  I tried having a bigger log message but that
-- crashed "mtn commit".)
--
-- The following 1024-byte block of nonsense is courtesy of the Eater
-- of Meaning.

message = [[
Won whip he keel, dens canal ply chafe hoots tooler gad nut pal hew
visa. Area dunn felts morrow exxon bib bern oz toil clot knightsbridge
malta tex organs. Respect, aft beehive, dip i mitchell walrus en BELY
Stow 5 pot juice 79 oberon preamble io Arabian, stag timers loy son
Paws fat i ram suez wiper, den ron unix minaret wire po fox sicilian
nubia puss fight. Oz sweeneys be mckee on paving eyers, em eel a
johnny thumbed illegal sash do ku 8,296 jazz, margo or rib ah fess
swine so vat up timeouts. Us drop fed hip beds ha i meaner (ax todays
suez of withdraw 53 electrify) dim en ah listings-cinnamon ad minded
Anu races nod abui a abide.

Epaulet if brisker de carnivals mae ed civic lettering alfa ale pandas
(micaa pry ada zoo pale or opt Tax Shy Slowed incapable), wee hem
bernoulli ott ha harlem he malabar sir Roach cortex ash avouch as Soda
Jimi. Ohm thief ox bay loops shaves gobi nu pi reese-ax-roe-waging
vieti salable bin love soft all oz oboe plush sniffed rowdy em
asserting used en sit ham land nat fly.
]]

for i = 1,6 do
   message = message .. message
end

mtn_setup()
addfile("file", "fnord")
for i = 1,4 do
   writefile("file", "commit "..tonumber(i).."\n")
   commit(nil, message)
end

check({"mkfifo", "fifo"}, 0, nil, nil)

-- We do this crazy thing because bg() can't redirect stdout.
-- FIXME: It should be able to redirect stdout.
-- FIXME: The quoting here will break if any of the strings
--        produced by mtn() contains a single quote.

proc = bg({"/bin/sh", "-c", 
	   "exec '"
	      .. table.concat(mtn("log", "--no-graph"), "' '")
	      .. "' >fifo"},
	  -SIGPIPE, nil, false)

p, e = io.open("fifo", "r")
if p == nil then err(e) end

-- Read 25 lines of text from the file.  This mimics the behavior of a
-- pager that is quit after one standard terminal screen.
for i = 1, 25 do
   p:read()
end
p:close()

proc:finish(3) -- three second timeout
