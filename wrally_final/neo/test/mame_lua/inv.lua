local m=manager.machine local sp=m.devices[":maincpu"].spaces["program"] local gf=0 local cpu=m.devices[":maincpu"]
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
local QT=tonumber(os.getenv("QT")) local QC=tonumber(os.getenv("QC"))
local ev={} local bad=0
local function chk(tag)
  local t=sp:read_u8(QT) local h=sp:read_u8(QT+1) local c=sp:read_u8(QC)
  ev[#ev+1]=string.format("gf%d %s t%d h%d c%d pc%06x",gf,tag,t,h,c,cpu.state["CURPC"].value) if #ev>30 then table.remove(ev,1) end
  if ((h+c)%2)~=t and bad<1 then bad=bad+1 print("---- INVARIANT BROKEN") print(table.concat(ev,"\n")) end
end
T1=sp:install_write_tap(QT,QT+1,"qt",function(o,d,mk) end)
emu.register_frame_done(function() if gf>=9000 or bad>0 then m:exit() end end)
-- poll after each write using a periodic check via write taps on q_cnt word
T2=sp:install_write_tap(QC&~1,(QC&~1)+1,"qc",function(o,d,mk) if (mk&0xff00)~=0 then local q=(d>>8)&0xff ev[#ev+1]=string.format("gf%d write q_cnt=%d (t%d h%d) pc%06x",gf,q,sp:read_u8(QT),sp:read_u8(QT+1),cpu.state["CURPC"].value) if #ev>30 then table.remove(ev,1) end
  local t=sp:read_u8(QT) local h=sp:read_u8(QT+1) if ((h+q)%2)~=t and bad<1 then bad=1 print("---- INVARIANT BROKEN at q_cnt write") print(table.concat(ev,"\n")) end end end)
T3=sp:install_write_tap(QT,QT+1,"ht",function(o,d,mk) local s="" if (mk&0xff00)~=0 then s=s.." tail="..((d>>8)&0xff) end if (mk&0xff)~=0 then s=s.." head="..(d&0xff) end ev[#ev+1]=string.format("gf%d write%s (c%d) pc%06x",gf,s,sp:read_u8(QC),cpu.state["CURPC"].value) if #ev>30 then table.remove(ev,1) end end)
