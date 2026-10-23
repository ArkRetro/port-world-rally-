local cpu=manager.machine.devices[":maincpu"]
local sp=cpu.spaces["program"]
local st=cpu.state["CURPC"]
local w={}
local n=0
local P=manager.machine.ioport.ports
tap=sp:install_write_tap(0x100000,0x103fff,"vw",function(o,d,m)
 local pc=st.value
 local k=string.format("%06x",pc)
 local e=w[k] or {c=0,lo=o,hi=o,m=m}
 e.c=e.c+1 if o<e.lo then e.lo=o end if o>e.hi then e.hi=o end
 w[k]=e
end)
local function set(field,v) for k,p in pairs(P) do for fn,f in pairs(p.fields) do if fn==field then f:set_value(v) end end end end
emu.register_frame_done(function() n=n+1
 if n==3000 then set("Coin 1",1) end
 if n==3006 then set("Coin 1",0) end
 if n==3060 then set("1 Player Start",1) end
 if n==3066 then set("1 Player Start",0) end
 if n>3100 then local r=(n//20)%4 set("P1 Right",r==0 and 1 or 0) set("P1 Left",r==2 and 1 or 0) set("P1 Button 1",(n//7)%2) set("P1 Button 2",(n//13)%2) end
 if n==7000 then
  local f=io.open(os.getenv("OUT").."/vw.txt","w") for k,e in pairs(w) do f:write(string.format("%s %d %06x %06x %x\n",k,e.c,e.lo,e.hi,e.m)) end f:close()
  manager.machine:exit() end end)
