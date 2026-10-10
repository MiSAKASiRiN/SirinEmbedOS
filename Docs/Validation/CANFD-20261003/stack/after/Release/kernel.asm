00000000 <system_thread_entry>:
       0: b510         	push	{r4, lr}
       2: 4604         	mov	r4, r0
       4: 7d00         	ldrb	r0, [r0, #0x14]
       6: 2801         	cmp	r0, #0x1
       8: d10b         	bne	0x22 <system_thread_entry+0x22> @ imm = #0x16
       a: 480b         	ldr	r0, [pc, #0x2c]         @ 0x38 <system_thread_entry+0x38>
       c: 2102         	movs	r1, #0x2
       e: 2203         	movs	r2, #0x3
      10: f04f 33ff    	mov.w	r3, #0xffffffff
      14: 6940         	ldr	r0, [r0, #0x14]
      16: f7ff fffe    	bl	0x16 <system_thread_entry+0x16> @ imm = #-0x4
			00000016:  R_ARM_THM_CALL	osEventFlagsWait
      1a: 4908         	ldr	r1, [pc, #0x20]         @ 0x3c <system_thread_entry+0x3c>
      1c: 4008         	ands	r0, r1
    ... omitted non-call instructions ...
      20: d104         	bne	0x2c <system_thread_entry+0x2c> @ imm = #0x8
      22: e9d4 1001    	ldrd	r1, r0, [r4, #4]
      26: e8bd 4010    	pop.w	{r4, lr}
      2a: 4708         	bx	r1
      2c: 4804         	ldr	r0, [pc, #0x10]         @ 0x40 <system_thread_entry+0x40>
      2e: 4905         	ldr	r1, [pc, #0x14]         @ 0x44 <system_thread_entry+0x44>
      30: 2276         	movs	r2, #0x76
      32: f7ff fffe    	bl	0x32 <system_thread_entry+0x32> @ imm = #-0x4
			00000032:  R_ARM_THM_CALL	SRNOS_KernelFailure
      36: bf00         	nop
      38: 00 00 00 00  	.word	0x00000000

