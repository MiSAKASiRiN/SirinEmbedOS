# source Tools/Debug/sensor_snapshot.gdb
# Target must be halted. srnos-sensor-stacks /tmp/srnos-before
# Reads target memory only; no target function calls, resume or reset.
define srnos-sensor-stacks
  printf "SRNOS_STACK_SCAN_BEGIN $arg0\n"
  dump binary memory $arg0-dtcm.bin 0x20000000 0x20020000
  dump binary memory $arg0-d1.bin 0x24000000 0x24050000
  set $list_index = 0
  while $list_index < 59
    if $list_index < 56
      set $list = &pxReadyTasksLists[$list_index]
    else
      if $list_index == 56
        set $list = &xDelayedTaskList1
      else
        if $list_index == 57
          set $list = &xDelayedTaskList2
        else
          set $list = &xSuspendedTaskList
        end
      end
    end
    set $node = $list->xListEnd.pxNext
    set $item_count = 0
    while $node != (ListItem_t*)&$list->xListEnd && $item_count < 64
      set $t = (TCB_t*)$node->pvOwner
      printf "SRNOS_STACK %s %u 0x%x 0x%x\n", $t->pcTaskName, $t->uxPriority, $t->pxStack, $t->pxEndOfStack+1
      set $node = $node->pxNext
      set $item_count = $item_count+1
    end
    set $list_index = $list_index+1
  end
  printf "SRNOS_STACK_SCAN_END $arg0\n"
end

define srnos-sensor-snapshot
  p srnos_sensor_bringup
  p uwTick
  p g_omx_dbg_spi[0]
  p g_omx_dbg_i2c[0]
  p g_omx_dbg_mcp2518fd[0]
  p g_srnos_fault_active
  p/x *(unsigned int *)0xe000ed28
  p/x *(unsigned int *)0xe000ed2c
end
