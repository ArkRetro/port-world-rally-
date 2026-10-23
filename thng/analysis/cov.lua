local cpu=manager.machine.devices[":maincpu"]
local sp=cpu.spaces["program"]
local st=cpu.state["CURPC"]
local ex,dat={},{}
local phase=2
local n=0
local P=manager.machine.ioport.ports
tap=sp:install_read_tap(0,0xfffff,"cov",function(o,d,m)
 local pc=st.value
 if o-pc>=0 and o-pc<=10 then ex[pc]=(ex[pc] or 0)|phase else dat[o&0xffffe]=(dat[o&0xffffe] or 0)|phase end
end)
local function set(port,field,v) for k,p in pairs(P) do for fn,f in pairs(p.fields) do if fn==field then f:set_value(v) end end end end
emu.register_frame_done(function() n=n+1
 if n==3000 then phase=4 set("Coin 1",nil,1) end
 if n==3000 then set(nil,"Coin 1",1) end
 if n==3006 then set(nil,"Coin 1",0) end
 if n==3060 then set(nil,"1 Player Start",1) end
 if n==3066 then set(nil,"1 Player Start",0) end
 if n>3100 then local r=(n//20)%4 set(nil,"P1 Right",r==0 and 1 or 0) set(nil,"P1 Left",r==2 and 1 or 0) set(nil,"P1 Button 1",(n//7)%2) set(nil,"P1 Button 2",(n//13)%2) set(nil,"P1 Up",(n//50)%2) end
 if n==7000 then
  local f=io.open(os.getenv("OUT").."/cov_exec.txt","w") for k,v in pairs(ex) do f:write(string.format("%06x %d\n",k,v)) end f:close()
  f=io.open(os.getenv("OUT").."/cov_data.txt","w") for k,v in pairs(dat) do f:write(string.format("%06x %d\n",k,v)) end f:close()
  manager.machine:snapshot() manager.machine:exit() end end)
