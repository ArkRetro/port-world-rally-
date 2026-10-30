local m=manager.machine local sp=m.devices[":maincpu"].spaces["program"] local f=0
local scr=nil for k,v in pairs(m.screens) do scr=v end
local A=tonumber(os.getenv("A"),16) local OUT=os.getenv("OUT") local NF=tonumber(os.getenv("NF")) local done=false
local log=io.open(OUT.."/log.txt","w")
emu.register_frame_done(function() f=f+1
  local v=sp:read_u16(A)
  if not done and v==1 then sp:write_u16(A,3) done=f end
  if f%50==0 then log:write(f.." "..v.."\n") end
  if done and f>done and f%60==0 then scr:snapshot(string.format("%s/%06d.png",OUT,f)) end
  if f>=NF then log:close() m:exit() end end)
