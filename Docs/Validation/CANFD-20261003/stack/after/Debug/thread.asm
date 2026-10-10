00000000 <trampoline>:
       0: e92d 4ff0    	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
       4: b08f         	sub	sp, #0x3c
       6: 4604         	mov	r4, r0
       8: f04f 30ff    	mov.w	r0, #0xffffffff
       c: 9000         	str	r0, [sp]
       e: ab0e         	add	r3, sp, #0x38
      10: 2001         	movs	r0, #0x1
      12: 2100         	movs	r1, #0x0
      14: f04f 32ff    	mov.w	r2, #0xffffffff
      18: 2601         	movs	r6, #0x1
      1a: 2500         	movs	r5, #0x0
      1c: f7ff fffe    	bl	0x1c <trampoline+0x1c>  @ imm = #-0x4
			0000001c:  R_ARM_THM_CALL	xTaskGenericNotifyWait
      20: 7c60         	ldrb	r0, [r4, #0x11]
      22: b178         	cbz	r0, 0x44 <trampoline+0x44> @ imm = #0x1e
      24: e9d4 1015    	ldrd	r1, r0, [r4, #84]
      28: 7526         	strb	r6, [r4, #0x14]
      2a: 4788         	blx	r1
      2c: f7ff fffe    	bl	0x2c <trampoline+0x2c>  @ imm = #-0x4
			0000002c:  R_ARM_THM_CALL	vPortEnterCritical
      30: 7425         	strb	r5, [r4, #0x10]
      32: f7ff fffe    	bl	0x32 <trampoline+0x32>  @ imm = #-0x4
			00000032:  R_ARM_THM_CALL	vPortExitCritical
      36: 2000         	movs	r0, #0x0
      38: f7ff fffe    	bl	0x38 <trampoline+0x38>  @ imm = #-0x4
			00000038:  R_ARM_THM_CALL	vTaskDelete
      3c: 980e         	ldr	r0, [sp, #0x38]
      3e: b00f         	add	sp, #0x3c
      40: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}
      44: f240 0600    	movw	r6, #0x0
			00000044:  R_ARM_THM_MOVW_ABS_NC	applications_ready
    ... omitted non-call instructions ...
      66: 9700         	str	r7, [sp]
      68: f7ff fffe    	bl	0x68 <trampoline+0x68>  @ imm = #-0x4
			00000068:  R_ARM_THM_CALL	xTaskGenericNotifyWait
      6c: 7830         	ldrb	r0, [r6]
      6e: 2801         	cmp	r0, #0x1
    ... omitted non-call instructions ...
      7c: b119         	cbz	r1, 0x86 <trampoline+0x86> @ imm = #0x6
      7e: 6ba0         	ldr	r0, [r4, #0x38]
      80: 4788         	blx	r1
      82: 4605         	mov	r5, r0
      84: e000         	b	0x88 <trampoline+0x88>  @ imm = #0x0
    ... omitted non-call instructions ...
      8e: 2501         	movne	r5, #0x1
      90: f7ff fffe    	bl	0x90 <trampoline+0x90>  @ imm = #-0x4
			00000090:  R_ARM_THM_CALL	xTaskGetTickCount
      94: f894 1044    	ldrb.w	r1, [r4, #0x44]
      98: f10d 0910    	add.w	r9, sp, #0x10
    ... omitted non-call instructions ...
      ac: 6ca1         	ldreq	r1, [r4, #0x48]
      ae: f7ff fffe    	bl	0xae <trampoline+0xae>  @ imm = #-0x4
			000000ae:  R_ARM_THM_CALL	SRNOS_CycleAttach
      b2: f10d 0a08    	add.w	r10, sp, #0x8
      b6: 2001         	movs	r0, #0x1
    ... omitted non-call instructions ...
      e6: 463b         	mov	r3, r7
      e8: f7ff fffe    	bl	0xe8 <trampoline+0xe8>  @ imm = #-0x4
			000000e8:  R_ARM_THM_CALL	SRNOS_ThreadWaitFlags
      ec: 1dc2         	adds	r2, r0, #0x7
      ee: d126         	bne	0x13e <trampoline+0x13e> @ imm = #0x4c
    ... omitted non-call instructions ...
      f2: e02b         	b	0x14c <trampoline+0x14c> @ imm = #0x56
      f4: f7ff fffe    	bl	0xf4 <trampoline+0xf4>  @ imm = #-0x4
			000000f4:  R_ARM_THM_CALL	xTaskGetTickCount
      f8: ebab 0000    	sub.w	r0, r11, r0
      fc: 2801         	cmp	r0, #0x1
      fe: db2d         	blt	0x15c <trampoline+0x15c> @ imm = #0x5a
     100: f7ff fffe    	bl	0x100 <trampoline+0x100> @ imm = #-0x4
			00000100:  R_ARM_THM_CALL	SRNOS_KernelTaskContext
     104: b350         	cbz	r0, 0x15c <trampoline+0x15c> @ imm = #0x54
     106: f7ff fffe    	bl	0x106 <trampoline+0x106> @ imm = #-0x4
			00000106:  R_ARM_THM_CALL	xTaskGetTickCount
     10a: ebab 0600    	sub.w	r6, r11, r0
     10e: 2e01         	cmp	r6, #0x1
     110: db24         	blt	0x15c <trampoline+0x15c> @ imm = #0x48
     112: f7ff fffe    	bl	0x112 <trampoline+0x112> @ imm = #-0x4
			00000112:  R_ARM_THM_CALL	xTaskGetCurrentTaskHandle
     116: 4601         	mov	r1, r0
     118: f240 0000    	movw	r0, #0x0
    ... omitted non-call instructions ...
     152: 4631         	mov	r1, r6
     154: f7ff fffe    	bl	0x154 <trampoline+0x154> @ imm = #-0x4
			00000154:  R_ARM_THM_CALL	delay_record
     158: 3007         	adds	r0, #0x7
     15a: d004         	beq	0x166 <trampoline+0x166> @ imm = #0x8
    ... omitted non-call instructions ...
     16c: 4659         	mov	r1, r11
     16e: f7ff fffe    	bl	0x16e <trampoline+0x16e> @ imm = #-0x4
			0000016e:  R_ARM_THM_CALL	SRNOS_CycleBegin
     172: 6b22         	ldr	r2, [r4, #0x30]
     174: 4651         	mov	r1, r10
     176: 6ba0         	ldr	r0, [r4, #0x38]
     178: 4790         	blx	r2
     17a: 4606         	mov	r6, r0
     17c: 4648         	mov	r0, r9
     17e: f7ff fffe    	bl	0x17e <trampoline+0x17e> @ imm = #-0x4
			0000017e:  R_ARM_THM_CALL	SRNOS_CycleEnd
     182: b15e         	cbz	r6, 0x19c <trampoline+0x19c> @ imm = #0x16
     184: 1eb0         	subs	r0, r6, #0x2
    ... omitted non-call instructions ...
     1ae: f8d4 9048    	ldr.w	r9, [r4, #0x48]
     1b2: f7ff fffe    	bl	0x1b2 <trampoline+0x1b2> @ imm = #-0x4
			000001b2:  R_ARM_THM_CALL	xTaskGetTickCount
     1b6: eb09 010b    	add.w	r1, r9, r11
     1ba: 1a40         	subs	r0, r0, r1
    ... omitted non-call instructions ...
     1d2: 4639         	mov	r1, r7
     1d4: f7ff fffe    	bl	0x1d4 <trampoline+0x1d4> @ imm = #-0x4
			000001d4:  R_ARM_THM_CALL	SRNOS_CycleSkipped
     1d8: 2100         	movs	r1, #0x0
     1da: 9f01         	ldr	r7, [sp, #0x4]
    ... omitted non-call instructions ...
     1e4: b109         	cbz	r1, 0x1ea <trampoline+0x1ea> @ imm = #0x2
     1e6: 6ba0         	ldr	r0, [r4, #0x38]
     1e8: 4788         	blx	r1
     1ea: f7ff fffe    	bl	0x1ea <trampoline+0x1ea> @ imm = #-0x4
			000001ea:  R_ARM_THM_CALL	vPortEnterCritical
     1ee: 07e8         	lsls	r0, r5, #0x1f
     1f0: f04f 0003    	mov.w	r0, #0x3
    ... omitted non-call instructions ...
     1fc: 6e20         	ldr	r0, [r4, #0x60]
     1fe: f7ff fffe    	bl	0x1fe <trampoline+0x1fe> @ imm = #-0x4
			000001fe:  R_ARM_THM_CALL	xEventGroupSetBits
     202: 2000         	movs	r0, #0x0
     204: f7ff fffe    	bl	0x204 <trampoline+0x204> @ imm = #-0x4
			00000204:  R_ARM_THM_CALL	vTaskSuspend
     208: f7ff fffe    	bl	0x208 <trampoline+0x208> @ imm = #-0x4
			00000208:  R_ARM_THM_CALL	vPortExitCritical
     20c: bf00         	nop
     20e: bf00         	nop
     210: 2000         	movs	r0, #0x0
     212: f7ff fffe    	bl	0x212 <trampoline+0x212> @ imm = #-0x4
			00000212:  R_ARM_THM_CALL	vTaskSuspend
     216: e7fb         	b	0x210 <trampoline+0x210> @ imm = #-0xa


00000000 <SRNOS_KernelRunLoop>:
       0: e92d 41f0    	push.w	{r4, r5, r6, r7, r8, lr}
       4: b08a         	sub	sp, #0x28
       6: 2800         	cmp	r0, #0x0
       8: bf1e         	ittt	ne
       a: 4604         	movne	r4, r0
       c: 6800         	ldrne	r0, [r0]
       e: 2800         	cmpne	r0, #0x0
      10: d10b         	bne	0x2a <SRNOS_KernelRunLoop+0x2a> @ imm = #0x16
      12: f240 0000    	movw	r0, #0x0
			00000012:  R_ARM_THM_MOVW_ABS_NC	.L.str.1
      16: f240 0100    	movw	r1, #0x0
    ... omitted non-call instructions ...
      22: f240 1227    	movw	r2, #0x127
      26: f7ff fffe    	bl	0x26 <SRNOS_KernelRunLoop+0x26> @ imm = #-0x4
			00000026:  R_ARM_THM_CALL	SRNOS_KernelFailure
      2a: 46e8         	mov	r8, sp
      2c: 68e1         	ldr	r1, [r4, #0xc]
      2e: 4640         	mov	r0, r8
      30: f7ff fffe    	bl	0x30 <SRNOS_KernelRunLoop+0x30> @ imm = #-0x4
			00000030:  R_ARM_THM_CALL	SRNOS_CycleAttach
      34: 2501         	movs	r5, #0x1
      36: f7ff fffe    	bl	0x36 <SRNOS_KernelRunLoop+0x36> @ imm = #-0x4
			00000036:  R_ARM_THM_CALL	xTaskGetTickCount
      3a: 4606         	mov	r6, r0
      3c: e00b         	b	0x56 <SRNOS_KernelRunLoop+0x56> @ imm = #0x16
    ... omitted non-call instructions ...
      42: 4631         	mov	r1, r6
      44: f7ff fffe    	bl	0x44 <SRNOS_KernelRunLoop+0x44> @ imm = #-0x4
			00000044:  R_ARM_THM_CALL	SRNOS_CycleBegin
      48: 6821         	ldr	r1, [r4]
      4a: 68a0         	ldr	r0, [r4, #0x8]
      4c: 4788         	blx	r1
      4e: 4640         	mov	r0, r8
      50: f7ff fffe    	bl	0x50 <SRNOS_KernelRunLoop+0x50> @ imm = #-0x4
			00000050:  R_ARM_THM_CALL	SRNOS_CycleEnd
      54: 2500         	movs	r5, #0x0
      56: 07ef         	lsls	r7, r5, #0x1f
    ... omitted non-call instructions ...
      62: b111         	cbz	r1, 0x6a <SRNOS_KernelRunLoop+0x6a> @ imm = #0x4
      64: 68a0         	ldr	r0, [r4, #0x8]
      66: 4788         	blx	r1
      68: b1b0         	cbz	r0, 0x98 <SRNOS_KernelRunLoop+0x98> @ imm = #0x2c
      6a: 68e0         	ldr	r0, [r4, #0xc]
    ... omitted non-call instructions ...
      72: d1e5         	bne	0x40 <SRNOS_KernelRunLoop+0x40> @ imm = #-0x36
      74: f7ff fffe    	bl	0x74 <SRNOS_KernelRunLoop+0x74> @ imm = #-0x4
			00000074:  R_ARM_THM_CALL	xTaskGetTickCount
      78: 68e1         	ldr	r1, [r4, #0xc]
      7a: 4607         	mov	r7, r0
    ... omitted non-call instructions ...
      8a: 4629         	mov	r1, r5
      8c: f7ff fffe    	bl	0x8c <SRNOS_KernelRunLoop+0x8c> @ imm = #-0x4
			0000008c:  R_ARM_THM_CALL	SRNOS_CycleSkipped
      90: 68e0         	ldr	r0, [r4, #0xc]
      92: fb00 6605    	mla	r6, r0, r5, r6
      96: e7d3         	b	0x40 <SRNOS_KernelRunLoop+0x40> @ imm = #-0x5a
      98: b00a         	add	sp, #0x28
      9a: e8bd 81f0    	pop.w	{r4, r5, r6, r7, r8, pc}

