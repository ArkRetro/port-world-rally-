-- P1/P2 read coverage at 256-byte granularity + P2 opcode fetch count
local n=0
local cov1={} ; local cov2={}; local p2f=0
local PC
local OUT = os.getenv("COVOUT") or "cov.txt"
local PLAY = tonumber(os.getenv("PLAYS") or "0")
local function dump()
  local f=io.open(OUT,"w")
  f:write("frame "..n.." p2fetch "..p2f.."\n")
  for b=0,4095 do if cov1[b] then f:write(string.format("1 %03X %d\n",b,cov1[b])) end end
  for b=0,4095 do if cov2[b] then f:write(string.format("2 %03X %d\n",b,cov2[b])) end end
  f:close()
end
local function install()
  local cpu=manager.machine.devices[":maincpu"]; local mem=cpu.spaces["program"]
  PC=cpu.state["CURPC"]
  TAP1=mem:install_read_tap(0x000000,0x0FFFFF,"c1",function(off,data,mask) local b=off>>8; cov1[b]=(cov1[b] or 0)+1; return data end)
  TAP2=mem:install_read_tap(0x200000,0x2FFFFF,"c2",function(off,data,mask)
     local b=(off-0x200000)>>8; cov2[b]=(cov2[b] or 0)+1
     local d=off-PC.value; if d>=0 and d<=10 then p2f=p2f+1 end
     return data end)
end
local ioport=manager.machine.ioport
local function fld(port,name) local p=ioport.ports[port]; if p then return p.fields[name] end end
local coin,start,btnA,right,left
local sched={}
local function press(f,fr,len) sched[#sched+1]={f=f,s=fr,e=fr+(len or 4)} end
CB=emu.register_frame_done(function()
  n=n+1
  if n==600 then install()
    coin=fld(":AUDIO_COIN","Coin 1"); start=fld(":edge:joy:START","1 Player Start")
    btnA=fld(":edge:joy:JOY1","P1 A"); right=fld(":edge:joy:JOY1","P1 Right"); left=fld(":edge:joy:JOY1","P1 Left")
    local base=3200
    for k=0,PLAY-1 do
      local t=base+k*7000
      press(coin,t,4); press(coin,t+40,4); press(start,t+120,4)
      for j=1,k do press(right,t+240+j*30,6) end
      press(btnA,t+600,4); press(start,t+640,4)
      press(btnA,t+700,5600)
    end
  end
  for _,s in ipairs(sched) do if s.f then if n==s.s then s.f:set_value(1) elseif n==s.e then s.f:set_value(0) end end end
  if n%3000==0 then dump() end
end)
