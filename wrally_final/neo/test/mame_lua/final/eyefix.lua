local m=manager.machine local f=0 local sp=m.devices[":maincpu"].spaces["program"]
local scr=nil for k,v in pairs(m.screens) do scr=v end
emu.register_frame_done(function() f=f+1
  if f==560 then scr:snapshot("intro.png")
    for col=0,39 do local s=""
      for row=0,31 do sp:write_u16(0x3C0000,0x7000+col*32+row) local w=sp:read_u16(0x3C0002) s=s..string.format("%04x ",w) end
      print(col,s) end
    m:exit() end end)
