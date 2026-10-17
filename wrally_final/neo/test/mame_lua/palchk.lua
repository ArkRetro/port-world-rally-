local m=manager.machine local gf=0 local sp=m.devices[":maincpu"].spaces["program"]
local hook=dofile("/tmp/claude-0/s/inp_rs.lua")
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
local F={start=S["P1 Start"],coin=S["P1 Select"],acc=J["P1 A"],left=J["P1 Left"],right=J["P1 Right"]}
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1 local st=hook(gf,sp,true) for k,f in pairs(F) do f:set_value(st[k] and 1 or 0) end end end)
local CT=tonumber(os.getenv("CT")) local G0=tonumber(os.getenv("G0")) local G1=tonumber(os.getenv("G1"))
emu.register_frame_done(function()
 if gf>=G0 and gf<=G1 then
  local bad={} 
  for p=0,47 do local nb=0 for c=0,15 do local g=sp:read_u16(0x104000+p*32+c*2)&0xfff local exp=sp:read_u16(CT+g*2) local ng=sp:read_u16(0x400200+p*32+c*2) if exp~=ng then nb=nb+1 end end if nb>0 then bad[#bad+1]=p..":"..nb end end
  print(gf, sp:read_u16(0x10a708), #bad, table.concat(bad," "))
 end
 if gf>G1 then m:exit() end end)
