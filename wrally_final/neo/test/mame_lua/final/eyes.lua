local m=manager.machine local f=0 local scr=nil for k,v in pairs(m.screens) do scr=v end
local OUT=os.getenv("OUT")
emu.register_frame_done(function() f=f+1
  if f%20==0 and f<=600 then scr:snapshot(OUT..string.format("/e%04d.png",f)) end
  if f>600 then m:exit() end end)
