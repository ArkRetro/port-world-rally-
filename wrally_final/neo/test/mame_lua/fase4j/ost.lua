local m=manager.machine local sp=m.devices[":maincpu"].spaces["program"] local f=0
local F=m.ioport.ports[":P1_P2"].fields
local scr=nil for k,v in pairs(m.screens) do scr=v end
local R=tonumber(os.getenv("R")) local S=tonumber(os.getenv("S")) local OUT=os.getenv("OUT")
local log=io.open(OUT..".txt","w")
local function press(n,on) F[n]:set_value(on and 1 or 0) end
emu.register_frame_done(function() f=f+1
  press("Coin 1",(f>=900 and f<912))
  press("1 Player Start",(f>=1100 and f<1112) or (f>=1300 and f<1312) or (f>=1500 and f<1512))
  press("P1 Button 1",(f>=1900 and f<1906))
  if f>=1950 and f<2000 then sp:write_u16(0xfef378,R) sp:write_u16(0xfef37a,S) sp:write_u8(0xfef373,R) end
  if f%10==0 and f>=2300 and f<=2800 then log:write(string.format("%d x=%04x y=%04x s198=%04x p=%04x%04x t=%08x\n",f,sp:read_u16(0xfeea98),sp:read_u16(0xfeea9c),sp:read_u16(0xfec198),sp:read_u16(0xfeea8c),sp:read_u16(0xfeea90),sp:read_u32(0xfec1aa))) end
  if f==2560 then scr:snapshot(OUT..".png") end
  if f>=2800 then log:close() m:exit() end end)
