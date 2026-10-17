local m=manager.machine local f=0 local gf=0 local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"]
local hook=dofile("/tmp/claude-0/s/inp_rs.lua")
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
local F={start=S["P1 Start"],coin=S["P1 Select"],acc=J["P1 A"],left=J["P1 Left"]}
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1 local st=hook(gf,sp,true) for k,f in pairs(F) do f:set_value(st[k] and 1 or 0) end end end)
local tf=0 local R={} local G={} local G0=tonumber(os.getenv("G0") or "6000") local G1=tonumber(os.getenv("G1") or "8000") local nf=0
local function acc(o,w)
  if gf<G0 then return end
  local line=(m.time:as_double()-tf)*59.185606*264
  local reg=(o<0x401000) and "pal" or ((o<0x401600) and "stage" or "stbuf1")
  local vis=(line>40.5 and line<263) and "VIS" or "vbl"
  local k=string.format("%s %s %s %06x",vis,reg,w,cpu.state["CURPC"].value) R[k]=(R[k] or 0)+1
  local g=vis.." "..reg G[g]=(G[g] or 0)+1 end
T1=sp:install_write_tap(0x400000,0x401fff,"pw",function(o,d,mk) acc(o,"W") end)
T2=sp:install_read_tap(0x400000,0x401fff,"pr",function(o,d,mk) acc(o,"R") return d end)
emu.register_frame_done(function() f=f+1 tf=m.time:as_double() if gf>=G0 then nf=nf+1 end
 if gf>G1 then
  print("frames",nf)
  for k,v in pairs(G) do print("G",k,v) end
  local ks={} for k,_ in pairs(R) do ks[#ks+1]=k end table.sort(ks,function(a,b) return R[a]>R[b] end)
  for i,k in ipairs(ks) do if i<=20 then print("PC",k,R[k]) end end
  m:exit() end end)
