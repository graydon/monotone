-- user interaction commands that produce a lot of output, like 'mtn
-- log', should die immediately on SIGPIPE (which they'll get if, for
-- instance, the user pipes the command to a pager, reads the first
-- few dozen lines, and then quits the pager).

skip_if(ostype=="Windows")
SIGPIPE = 13 -- what it is on traditional Unixy systems

-- Testing this correctly involves avoiding a number of race
-- conditions.  This is the behavior we want:
--
--    parent               child
--    ------               -----
--    fork
--                         exec
--    open pipe for read   open pipe for write
--                         write to pipe, which blocks
--    close pipe
--                         (kernel generates SIGPIPE)
--
-- There is a synchronization point at the open()s, but it is
-- essential to ensure that the child's write operation fills the
-- kernel's pipe buffer and blocks.  If we did not fill the pipe
-- buffer, the write could complete and return before the parent
-- closes its end of the pipe, causing the signal not to be delivered.
--
-- Kernel pipe buffers are known to be as big as 64K.  Thus, it is
-- overkill to make four commits each with an 64K log message, but I
-- like me some overkill.  (There is a temptation to make it sixteen;
-- I can imagine someone deciding to bulk up the pipe buffer to a nice
-- round megabyte.  But the time this test takes is proportional to
-- the number of commits, so I won't.  I tried having a bigger log
-- message but that crashed "mtn commit".)
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

logcmd = "exec '" .. table.concat(mtn("log", "--no-graph"), "' '") .. "' >fifo"

proc = bg({"/bin/sh", "-c", logcmd}, -SIGPIPE, nil, false)

p, e = io.open("fifo", "r")
if p == nil then err(e) end
p:close()
proc:finish(3) -- three second timeout
