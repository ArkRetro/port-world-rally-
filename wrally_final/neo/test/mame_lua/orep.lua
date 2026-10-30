local m=manager.machine local sp=m.devices[":maincpu"].spaces["program"] local gf=0 local c=0
local S=m.ioport.ports[":P1_P2"].fields
TG=sp:install_write_tap(0xfec1a6,0xfec1a7,"g",function(o,d,mk) if d==0 then gf=gf+1 c=c+1
  S["1 Player Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["Coin 1"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  S["P1 Button 1"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  S["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
local H={} local nv=0 local seq={}
emu.register_frame_done(function()
 if gf>=5600 then nv=nv+1 H[c]=(H[c] or 0)+1 if c==0 then seq[#seq+1]=gf end end c=0
 if gf>=9000 then for k,n in pairs(H) do print("D",k,n) end print("vblanks",nv) print(table.concat(seq," ")) m:exit() end end)
