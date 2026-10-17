local m=manager.machine local sp=m.devices[":maincpu"].spaces["program"] local f=0
local J=m.ioport.ports[":edge:joy:JOY1"].fields
local S=m.ioport.ports[":edge:joy:START"].fields
local CO=m.ioport.ports[":AUDIO_COIN"].fields
local OUT=os.getenv("OUT") local NF=tonumber(os.getenv("NF"))
local log=io.open(OUT.."/log.txt","w")
local function press(t,n,on) t[n]:set_value(on and 1 or 0) end
local lt=-1 local lf=0 local g0=nil local race=0 local lastr=-1 local ended=nil
emu.register_frame_done(function() f=f+1
  local t=sp:read_u32(0x10C1AA)
  if g0==nil and t~=0 and f>4000 then g0=f if race==0 then race=1 end end
  if g0~=nil and t==0 then g0=nil ended=f end
  local inc=sp:read_u32(0x10C1AE) local r=-1 if g0 and inc>0 then r=t//inc end if g0 and r<0 then r=0 end
  local inrace = r>=0 and r<1500
  if t~=lt then lt=t lf=f end
  local cont = (f-lf>60) or f>4000 and (r>=1500 or g0==nil)
  CO["Coin 1"]:set_value(((f>=3000 and f<3006) or (cont and f%400<6)) and 1 or 0)
  press(S,"1 Player Start",(f>=3100 and f<3110) or (f>=3300 and f<3310) or (cont and f%400>=100 and f%400<110))
  press(J,"P1 Right",(f>=3500 and f<3506) or (inrace and (r%240)>=120 and (r%240)<150))
  press(J,"P1 Left",inrace and (r%240)<30)
  press(J,"P1 A",(f>=3600 and f<3606) or (inrace and r%600<560) or (cont and f%400>=200 and f%400<206))
  if r>=0 and lastr>=0 and r<lastr then race=race+1 end
  if r>=0 and r<1500 and r~=lastr then lastr=r local g={} for i=0,13 do g[#g+1]=string.format("%02x",sp:read_u16(0x10EB5C+2*i)&0xff) end
    log:write(string.format("%d %d x=%04x y=%04x d=%04x v=%04x G=%s\n",race,r,sp:read_u16(0x10EA98),sp:read_u16(0x10EA9C),sp:read_u16(0x10EAB0),sp:read_u16(0x10EAD2),table.concat(g,""))) end
  if f>=NF then log:close() m:exit() end end)
