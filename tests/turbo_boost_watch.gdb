set pagination off
set confirm off
set breakpoint pending on
set $race_ticks = 0
set $start_seen = 0
set $race_seen = 0

# Install the helper breakpoint after the executable has loaded.  This avoids
# relying on a fixed ASLR address or on GDB's incomplete PE symbol index.
start
break aero_turbo_boost_tick
commands
  silent
  set $rdram = $rcx
  set $ctx = $rdx
  set $car = *(unsigned long long*)($ctx + 128)
  set $phase = *(unsigned int*)($rdram + (0xFFFFFFFF8013FF88 - 0xFFFFFFFF80000000))
  set $flags = *(unsigned int*)($rdram + (($car + 0x34) - 0xFFFFFFFF80000000))
  set $timer = *(unsigned char*)($rdram + ((($car + 0x55) ^ 3) - 0xFFFFFFFF80000000))
  if ($phase == 3)
    set $race_ticks = $race_ticks + 1
    if ($race_ticks <= 12 && ($flags & 0x20000000) != 0 && $start_seen == 0)
      set $start_seen = 1
      printf "[turbo-harness] start boost awarded tick=%u flags=%08x\n", $race_ticks, $flags
    end
    if ($timer != 0 && $race_seen == 0)
      set $race_seen = 1
      printf "[turbo-harness] race turbo awarded timer=%u\n", $timer
    end
    if ($start_seen != 0 && $race_seen != 0)
      printf "[turbo-harness] PASS start_boost=1 race_turbo=1\n"
      set $race_seen = 2
    end
  end
  continue
end
continue
