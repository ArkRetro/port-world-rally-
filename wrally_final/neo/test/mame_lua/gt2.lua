local m=manager.machine local gf=0 local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"] local n=0
local hook=dofile("/tmp/claude-0/s/inp_rs.lua")
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
local F={start=S["P1 Start"],coin=S["P1 Select"],acc=J["P1 A"],left=J["P1 Left"]}
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1 local st=hook(gf,sp,true) for k,f in pairs(F) do f:set_value(st[k] and 1 or 0) end end end)
local R={} local T={}
for b=2,7 do local a=0x104000+b*0x800+0x600
 T[#T+1]=sp:install_write_tap(a,a+0x1ff,"w"..b,function(o,d,mk) local pc=cpu.state["CURPC"].value if pc<0x200000 then local k=string.format("b%d pc %06x",b,pc) R[k]=(R[k] or 0)+1 end end)
 T[#T+1]=sp:install_read_tap(a,a+0x1ff,"r"..b,function(o,d,mk) local pc=cpu.state["CURPC"].value if pc<0x200000 then local k=string.format("R b%d pc %06x",b,pc) R[k]=(R[k] or 0)+1 end return d end)
end
emu.register_frame_done(function() n=n+1 if gf>tonumber(os.getenv("G1") or "12000") then for k,v in pairs(R) do print("GT",k,v) end print("GTEND",gf) m:exit() end end)
