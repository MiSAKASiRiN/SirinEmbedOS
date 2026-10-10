00000000 <system_thread_entry>:
       0: b510         	push	{r4, lr}
       2: 4604         	mov	r4, r0
       4: 7d00         	ldrb	r0, [r0, #0x14]
       6: 2801         	cmp	r0, #0x1
       8: d110         	bne	0x2c <system_thread_entry+0x2c> @ imm = #0x20
       a: f240 0000    	movw	r0, #0x0
			0000000a:  R_ARM_THM_MOVW_ABS_NC	application_ready
       e: 2102         	movs	r1, #0x2
      10: f2c0 0000    	movt	r0, #0x0
			00000010:  R_ARM_THM_MOVT_ABS	application_ready
      14: 2203         	movs	r2, #0x3
    ... omitted non-call instructions ...
      1a: 6800         	ldr	r0, [r0]
      1c: f7ff fffe    	bl	0x1c <system_thread_entry+0x1c> @ imm = #-0x4
			0000001c:  R_ARM_THM_CALL	osEventFlagsWait
      20: 2102         	movs	r1, #0x2
      22: f2c8 0100    	movt	r1, #0x8000
    ... omitted non-call instructions ...
      2a: d104         	bne	0x36 <system_thread_entry+0x36> @ imm = #0x8
      2c: e9d4 1001    	ldrd	r1, r0, [r4, #4]
      30: e8bd 4010    	pop.w	{r4, lr}
      34: 4708         	bx	r1
      36: f240 0000    	movw	r0, #0x0
			00000036:  R_ARM_THM_MOVW_ABS_NC	.L.str.44
    ... omitted non-call instructions ...
      46: 2276         	movs	r2, #0x76
      48: f7ff fffe    	bl	0x48 <system_thread_entry+0x48> @ imm = #-0x4
			00000048:  R_ARM_THM_CALL	SRNOS_KernelFailure

Disassembly of section .text.application_thread:

