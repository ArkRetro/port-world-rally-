local cpu = manager.machine.devices[":maincpu"]
local ioports = manager.machine.ioport.ports
local function F(t,n) return ioports[t].fields[n] end
local coin, st, A = F(":AUDIO_COIN","Coin 1"), F(":edge:joy:START","1 Player Start"), F(":edge:joy:JOY1","P1 A")
local fr = 0
emu.register_frame_done(function()
  fr = fr + 1
  coin:set_value((fr>1000 and fr%500>=20 and fr%500<30) and 1 or 0)
  st:set_value((fr>1000 and fr%500>=100 and fr%500<110) and 1 or 0)
  A:set_value((fr>1000 and fr%60<8) and 1 or 0)
  if fr % 300 == 0 then print(string.format("FR %d t=%.2f pc=%06x", fr, manager.machine.time:as_double(), cpu.state["CURPC"].value)) end
end)
pcall(function() emu.add_machine_reset_notifier(function() print("MACHINE RESET frame "..fr) end) end)
emu.register_frame_done(function()
  if fr>1000 and fr%250==0 then manager.machine.video:snapshot() end
end)
