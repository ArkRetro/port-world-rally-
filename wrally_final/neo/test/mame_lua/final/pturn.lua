local m=manager.machine local sp=m.devices[":maincpu"].spaces["program"] local f=0
local J=m.ioport.ports[":edge:joy:JOY1"].fields
local S0=m.ioport.ports[":edge:joy:START"].fields
local CO=m.ioport.ports[":AUDIO_COIN"].fields
local scr=nil for k,v in pairs(m.screens) do scr=v end
local R=tonumber(os.getenv("R")) local S=tonumber(os.getenv("S")) local OUT=os.getenv("OUT")
local NPL=tonumber(os.getenv("NPL") or "0")
local log=io.open(OUT.."/log.txt","w")
local function press(t,n,on) t[n]:set_value(on and 1 or 0) end
emu.register_frame_done(function() f=f+1
  CO["Coin 1"]:set_value((f>=3000 and f<3006) and 1 or 0)
  press(S0,"1 Player Start",(f>=3100 and f<3110) or (f>=3300 and f<3310))
  press(J,"P1 A",(f>=3600 and f<3606) or (f>=4350))
  local ph=(f-4400)%160
  press(J,"P1 Left",f>=4400 and ph<40)
  press(J,"P1 Right",f>=4400 and ph>=80 and ph<120)
  if f>=3650 and f<3700 then sp:write_u16(0x10f378,R) sp:write_u16(0x10f37a,S) sp:write_u8(0x10f373,R) end
  if f>=4400 and f%8==0 then scr:snapshot(OUT..string.format("/f%05d.png",f))
    if NPL>0 then log:write(string.format("%d npl=%d\n",f,sp:read_u16(NPL))) end end
  if f>=5400 then log:close() m:exit() end end)
