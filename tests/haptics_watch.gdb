set pagination off
set confirm off
start
set $impact_seen = 0
set $turbo_seen = 0
break aero_haptics_race_tick
commands
 silent
 set $ram = $rcx
 set $context = $rdx
 set $car = *(unsigned long long*)($context + 128)
 set $offset = $car - 0xFFFFFFFF80000000
 if (*(unsigned int*)($ram + 0x13FF88) == 3 && *(unsigned int*)($ram + $offset + 4) == 0x8005C750)
  if (*(float*)($ram + $offset + 0x24) > 0 && $impact_seen == 0)
   set $impact_seen = 1
   printf "[haptics-test] impact observed\n"
  end
  if (*(unsigned char*)($ram + (($offset + 0x55) ^ 3)) > 0 && $turbo_seen == 0)
   set $turbo_seen = 1
   printf "[haptics-test] turbo observed\n"
  end
  if ($impact_seen && $turbo_seen)
   printf "[haptics-test] PASS both race events reached haptic hook\n"
   disable 2
  end
 end
 continue
end
continue
