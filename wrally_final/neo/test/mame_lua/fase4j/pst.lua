local m=manager.machine local sp=m.devices[":maincpu"].spaces["program"] local f=0
local J=m.ioport.ports[":edge:joy:JOY1"].fields
local S0=m.ioport.ports[":edge:joy:START"].fields
local CO=m.ioport.ports[":AUDIO_COIN"].fields
local scr=nil for k,v in pairs(m.screens) do scr=v end
local R=tonumber(os.getenv("R")) local S=tonumber(os.getenv("S")) local OUT=os.getenv("OUT")
local log=io.open(OUT..".txt","w")
local function press(t,n,on) t[n]:set_value(on and 1 or 0) end
emu.register_frame_done(function() f=f+1
  CO["Coin 1"]:set_value((f>=3000 and f<3006) and 1 or 0)
  press(S0,"1 Player Start",(f>=3100 and f<3110) or (f>=3300 and f<3310))
  press(J,"P1 A",(f>=3600 and f<3606))
  if f>=3650 and f<3700 then sp:write_u16(0x10f378,R) sp:write_u16(0x10f37a,S) sp:write_u8(0x10f373,R) end
  if f%10==0 and f>=4100 and f<=4600 then log:write(string.format("%d x=%04x y=%04x s198=%04x p=%04x%04x t=%08x\n",f,sp:read_u16(0x10ea98),sp:read_u16(0x10ea9c),sp:read_u16(0x10c198),sp:read_u16(0x10ea8c),sp:read_u16(0x10ea90),sp:read_u32(0x10c1aa))) end
  if f==4300 then scr:snapshot(OUT..".png") end
  if f>=4600 then log:close() m:exit() end end)
