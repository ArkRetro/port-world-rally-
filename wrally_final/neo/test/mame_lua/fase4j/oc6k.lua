local m=manager.machine local sp=m.devices[":maincpu"].spaces["program"] local f=0
local F=m.ioport.ports[":P1_P2"].fields
local OUT=os.getenv("OUT") local NF=tonumber(os.getenv("NF"))
local log=io.open(OUT.."/log.txt","w")
local function press(n,on) F[n]:set_value(on and 1 or 0) end
local lt=-1 local lf=0 local g0=nil local race=0 local lastr=-1
emu.register_frame_done(function() f=f+1
  local t=sp:read_u32(0xfec1aa)
  if g0==nil and t~=0 and f>2000 then g0=f if race==0 then race=1 end end
  if g0~=nil and t==0 then g0=nil end
  local inc=sp:read_u32(0xfec1ae) local r=-1 if g0 and inc>0 then r=t//inc end if g0 and r<0 then r=0 end
  local inrace = r>=0 and r<1500
  if t~=lt then lt=t lf=f end
  local cont = (f-lf>60) or f>2600 and (r>=1500 or g0==nil)
  press("Coin 1",(f>=900 and f<912) or (cont and f%400<6))
  press("1 Player Start",(f>=1100 and f<1112) or (f>=1300 and f<1312) or (f>=1500 and f<1512) or (cont and f%400>=100 and f%400<110))
  press("P1 Right",(f>=1800 and f<1806) or (inrace and (r%240)>=120 and (r%240)<150))
  press("P1 Left",inrace and (r%240)<30)
  press("P1 Button 1",(f>=1900 and f<1906) or (inrace and r%600<560) or (cont and f%400>=200 and f%400<206))
  if r>=0 and lastr>=0 and r<lastr then race=race+1 end
  if r>=0 and r<1500 and r~=lastr then lastr=r local g={} for i=0,13 do g[#g+1]=string.format("%02x",sp:read_u16(0xfeeb5c+2*i)&0xff) end
    log:write(string.format("%d %d x=%04x y=%04x d=%04x v=%04x G=%s\n",race,r,sp:read_u16(0xfeea98),sp:read_u16(0xfeea9c),sp:read_u16(0xfeeab0),sp:read_u16(0xfeead2),table.concat(g,""))) end
  if f>=NF then log:close() m:exit() end end)
