local m = manager.machine
local F = m.ioport.ports[":P1_P2"].fields
local d = m.debugger
local frame = 0
local N = tonumber(os.getenv("COVN") or "12000")
local mode = os.getenv("COVMODE") or "play"
local function press(name, on) F[name]:set_value(on and 1 or 0) end
emu.register_frame_done(function()
  frame = frame + 1
  if frame == 1 then d:command("trace /home/claude/wrally/cov/fifo,:maincpu,noloop") end
  if mode == "play" then
    press("Coin 1", (frame >= 900 and frame < 912) or (frame >= 960 and frame < 972))
    press("1 Player Start", frame >= 1100 and frame < 1112)
    local racing = frame > 1300
    press("P1 Button 1", racing and (frame % 600) < 560)
    press("P1 Left", racing and (frame % 240) < 30)
    press("P1 Right", racing and (frame % 240) >= 120 and (frame % 240) < 150)
    press("P1 Button 2", racing and (frame % 900) > 850)
  end
  if frame == N then d:command("trace off,:maincpu"); m:exit() end
end)
