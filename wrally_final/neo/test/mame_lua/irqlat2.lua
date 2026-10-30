local m=manager.machine local f=0 local gf=0 local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"]
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
PC0=0 SR0=0 SP0=0 RET0=0 local H={} local n=0 local tf=0 local lat={}
-- entrada del vblank: escritura del ack de IRQ (0x3C000C = 4) en ng_vblank? usamos PM? medimos con tap en 0x3C000C
TA=sp:install_write_tap(0x3c000c,0x3c000d,"a",function(o,d,mk) if gf>=5600 and (d&4)~=0 then local l=(m.time:as_double()-tf)*59.185606*264 local b=math.floor(l/5)*5 lat[b]=(lat[b] or 0)+1 if l>20 then print("LATE",string.format("%.0f lines gf %d at-vbl pc %06x sr %04x sp %06x ret %08x",l,gf,PC0,SR0,SP0,RET0)) end end end)
emu.register_frame_done(function() f=f+1 tf=m.time:as_double() PC0=cpu.state["CURPC"].value SR0=cpu.state["SR"].value SP0=cpu.state["SP"].value RET0=sp:read_u32(SP0)
 if gf>=5600 then local sr=cpu.state["SR"].value if (sr&0x700)~=0 then local pc=cpu.state["CURPC"].value local k=string.format("%06x ipl%d",pc,(sr>>8)&7) H[k]=(H[k] or 0)+1 n=n+1 end end
 if gf>=9000 then local ks={} for k,_ in pairs(H) do ks[#ks+1]=k end table.sort(ks,function(a,b) return H[a]>H[b] end)
  for i=1,math.min(25,#ks) do print("MASK",ks[i],H[ks[i]]) end print("masked frames",n,"of",f)
  local ls={} for k,_ in pairs(lat) do ls[#ls+1]=k end table.sort(ls) for _,k in ipairs(ls) do print("LAT",k,lat[k]) end m:exit() end end)
