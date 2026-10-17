local m=manager.machine local f=0
local scr=nil for k,v in pairs(m.screens) do scr=v end
local OUT=os.getenv("OUT") local NF=tonumber(os.getenv("NF")) local E0=tonumber(os.getenv("E0"))
local R=os.getenv("PATCH")
emu.register_frame_done(function() f=f+1
  if f==2 and R then local rg=m.memory.regions[":maincpu"] rg:write_u16(0xfc6e,0x6100) rg:write_u16(0xfc70,0xf836) end
  if f>=E0 and f%30==0 then scr:snapshot(string.format("%s/%06d.png",OUT,f)) end
  if f>=NF then m:exit() end end)
