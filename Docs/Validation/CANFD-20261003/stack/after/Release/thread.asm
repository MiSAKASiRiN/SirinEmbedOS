00000000 <trampoline>:
       0: e92d 4ff0    	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
       4: b08f         	sub	sp, #0x3c
       6: 4604         	mov	r4, r0
       8: f04f 36ff    	mov.w	r6, #0xffffffff
       c: 9600         	str	r6, [sp]
       e: ab0e         	add	r3, sp, #0x38
      10: 2001         	movs	r0, #0x1
      12: 2100         	movs	r1, #0x0
      14: f04f 32ff    	mov.w	r2, #0xffffffff
      18: 2701         	movs	r7, #0x1
      1a: 2500         	movs	r5, #0x0
      1c: f7ff fffe    	bl	0x1c <trampoline+0x1c>  @ imm = #-0x4
			0000001c:  R_ARM_THM_CALL	xTaskGenericNotifyWait
      20: 7c60         	ldrb	r0, [r4, #0x11]
      22: b170         	cbz	r0, 0x42 <trampoline+0x42> @ imm = #0x1c
      24: e9d4 1015    	ldrd	r1, r0, [r4, #84]
      28: 7527         	strb	r7, [r4, #0x14]
      2a: 4788         	blx	r1
      2c: f7ff fffe    	bl	0x2c <trampoline+0x2c>  @ imm = #-0x4
			0000002c:  R_ARM_THM_CALL	vPortEnterCritical
      30: 7425         	strb	r5, [r4, #0x10]
      32: f7ff fffe    	bl	0x32 <trampoline+0x32>  @ imm = #-0x4
			00000032:  R_ARM_THM_CALL	vPortExitCritical
      36: 2000         	movs	r0, #0x0
      38: f7ff fffe    	bl	0x38 <trampoline+0x38>  @ imm = #-0x4
			00000038:  R_ARM_THM_CALL	vTaskDelete
      3c: b00f         	add	sp, #0x3c
      3e: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}
      42: 4f56         	ldr	r7, [pc, #0x158]        @ 0x19c <trampoline+0x19c>
      44: ad0e         	add	r5, sp, #0x38
    ... omitted non-call instructions ...
      58: 9600         	str	r6, [sp]
      5a: f7ff fffe    	bl	0x5a <trampoline+0x5a>  @ imm = #-0x4
			0000005a:  R_ARM_THM_CALL	xTaskGenericNotifyWait
      5e: e7f2         	b	0x46 <trampoline+0x46>  @ imm = #-0x1c
      60: 7ce0         	ldrb	r0, [r4, #0x13]
    ... omitted non-call instructions ...
      6c: b119         	cbz	r1, 0x76 <trampoline+0x76> @ imm = #0x6
      6e: 6ba0         	ldr	r0, [r4, #0x38]
      70: 4788         	blx	r1
      72: 4682         	mov	r10, r0
      74: e001         	b	0x7a <trampoline+0x7a>  @ imm = #0x2
    ... omitted non-call instructions ...
      84: f04f 0a01    	movne.w	r10, #0x1
      88: f7ff fffe    	bl	0x88 <trampoline+0x88>  @ imm = #-0x4
			00000088:  R_ARM_THM_CALL	xTaskGetTickCount
      8c: 4607         	mov	r7, r0
      8e: f894 0044    	ldrb.w	r0, [r4, #0x44]
    ... omitted non-call instructions ...
      a0: 4630         	mov	r0, r6
      a2: f7ff fffe    	bl	0xa2 <trampoline+0xa2>  @ imm = #-0x4
			000000a2:  R_ARM_THM_CALL	SRNOS_CycleAttach
      a6: 2001         	movs	r0, #0x1
      a8: f10d 0908    	add.w	r9, sp, #0x8
    ... omitted non-call instructions ...
      d6: 2500         	movs	r5, #0x0
      d8: f7ff fffe    	bl	0xd8 <trampoline+0xd8>  @ imm = #-0x4
			000000d8:  R_ARM_THM_CALL	SRNOS_ThreadWaitFlags
      dc: 1dc1         	adds	r1, r0, #0x7
      de: d042         	beq	0x166 <trampoline+0x166> @ imm = #0x84
    ... omitted non-call instructions ...
      ea: e00a         	b	0x102 <trampoline+0x102> @ imm = #0x14
      ec: f7ff fffe    	bl	0xec <trampoline+0xec>  @ imm = #-0x4
			000000ec:  R_ARM_THM_CALL	xTaskGetTickCount
      f0: 1a38         	subs	r0, r7, r0
      f2: 2801         	cmp	r0, #0x1
    ... omitted non-call instructions ...
      f6: 4638         	mov	r0, r7
      f8: f7ff fffe    	bl	0xf8 <trampoline+0xf8>  @ imm = #-0x4
			000000f8:  R_ARM_THM_CALL	SRNOS_DelayUntil
      fc: 3007         	adds	r0, #0x7
      fe: d02f         	beq	0x160 <trampoline+0x160> @ imm = #0x5e
    ... omitted non-call instructions ...
     10c: 4639         	mov	r1, r7
     10e: f7ff fffe    	bl	0x10e <trampoline+0x10e> @ imm = #-0x4
			0000010e:  R_ARM_THM_CALL	SRNOS_CycleBegin
     112: 6b22         	ldr	r2, [r4, #0x30]
     114: 6ba0         	ldr	r0, [r4, #0x38]
     116: 4649         	mov	r1, r9
     118: 4790         	blx	r2
     11a: 4605         	mov	r5, r0
     11c: 4630         	mov	r0, r6
     11e: f7ff fffe    	bl	0x11e <trampoline+0x11e> @ imm = #-0x4
			0000011e:  R_ARM_THM_CALL	SRNOS_CycleEnd
     122: b9c5         	cbnz	r5, 0x156 <trampoline+0x156> @ imm = #0x30
     124: f894 0044    	ldrb.w	r0, [r4, #0x44]
    ... omitted non-call instructions ...
     136: 4447         	add	r7, r8
     138: f7ff fffe    	bl	0x138 <trampoline+0x138> @ imm = #-0x4
			00000138:  R_ARM_THM_CALL	xTaskGetTickCount
     13c: 1bc0         	subs	r0, r0, r7
     13e: bf5a         	itte	pl
    ... omitted non-call instructions ...
     14a: 4630         	mov	r0, r6
     14c: f7ff fffe    	bl	0x14c <trampoline+0x14c> @ imm = #-0x4
			0000014c:  R_ARM_THM_CALL	SRNOS_CycleSkipped
     150: fb05 7708    	mla	r7, r5, r8, r7
     154: e7ae         	b	0xb4 <trampoline+0xb4>  @ imm = #-0xa4
    ... omitted non-call instructions ...
     16c: b109         	cbz	r1, 0x172 <trampoline+0x172> @ imm = #0x2
     16e: 6ba0         	ldr	r0, [r4, #0x38]
     170: 4788         	blx	r1
     172: 2603         	movs	r6, #0x3
     174: 2d00         	cmp	r5, #0x0
    ... omitted non-call instructions ...
     178: 2604         	movne	r6, #0x4
     17a: f7ff fffe    	bl	0x17a <trampoline+0x17a> @ imm = #-0x4
			0000017a:  R_ARM_THM_CALL	vPortEnterCritical
     17e: 6e20         	ldr	r0, [r4, #0x60]
     180: 2101         	movs	r1, #0x1
     182: 7526         	strb	r6, [r4, #0x14]
     184: f7ff fffe    	bl	0x184 <trampoline+0x184> @ imm = #-0x4
			00000184:  R_ARM_THM_CALL	xEventGroupSetBits
     188: 2000         	movs	r0, #0x0
     18a: f7ff fffe    	bl	0x18a <trampoline+0x18a> @ imm = #-0x4
			0000018a:  R_ARM_THM_CALL	vTaskSuspend
     18e: f7ff fffe    	bl	0x18e <trampoline+0x18e> @ imm = #-0x4
			0000018e:  R_ARM_THM_CALL	vPortExitCritical
     192: 2000         	movs	r0, #0x0
     194: f7ff fffe    	bl	0x194 <trampoline+0x194> @ imm = #-0x4
			00000194:  R_ARM_THM_CALL	vTaskSuspend
     198: e7fb         	b	0x192 <trampoline+0x192> @ imm = #-0xa
     19a: bf00         	nop

00000000 <SRNOS_KernelRunLoop>:
       0: b5f0         	push	{r4, r5, r6, r7, lr}
       2: b08b         	sub	sp, #0x2c
       4: b3d0         	cbz	r0, 0x7c <SRNOS_KernelRunLoop+0x7c> @ imm = #0x74
       6: 4604         	mov	r4, r0
       8: 6800         	ldr	r0, [r0]
       a: b3b8         	cbz	r0, 0x7c <SRNOS_KernelRunLoop+0x7c> @ imm = #0x6e
       c: 466d         	mov	r5, sp
       e: 68e1         	ldr	r1, [r4, #0xc]
      10: 4628         	mov	r0, r5
      12: f7ff fffe    	bl	0x12 <SRNOS_KernelRunLoop+0x12> @ imm = #-0x4
			00000012:  R_ARM_THM_CALL	SRNOS_CycleAttach
      16: 2701         	movs	r7, #0x1
      18: f7ff fffe    	bl	0x18 <SRNOS_KernelRunLoop+0x18> @ imm = #-0x4
			00000018:  R_ARM_THM_CALL	xTaskGetTickCount
      1c: 4606         	mov	r6, r0
      1e: 07f8         	lsls	r0, r7, #0x1f
    ... omitted non-call instructions ...
      2a: b111         	cbz	r1, 0x32 <SRNOS_KernelRunLoop+0x32> @ imm = #0x4
      2c: 68a0         	ldr	r0, [r4, #0x8]
      2e: 4788         	blx	r1
      30: b310         	cbz	r0, 0x78 <SRNOS_KernelRunLoop+0x78> @ imm = #0x44
      32: 68e0         	ldr	r0, [r4, #0xc]
    ... omitted non-call instructions ...
      3e: d10f         	bne	0x60 <SRNOS_KernelRunLoop+0x60> @ imm = #0x1e
      40: f7ff fffe    	bl	0x40 <SRNOS_KernelRunLoop+0x40> @ imm = #-0x4
			00000040:  R_ARM_THM_CALL	xTaskGetTickCount
      44: 68e1         	ldr	r1, [r4, #0xc]
      46: 440e         	add	r6, r1
    ... omitted non-call instructions ...
      54: 4639         	mov	r1, r7
      56: f7ff fffe    	bl	0x56 <SRNOS_KernelRunLoop+0x56> @ imm = #-0x4
			00000056:  R_ARM_THM_CALL	SRNOS_CycleSkipped
      5a: 68e0         	ldr	r0, [r4, #0xc]
      5c: fb00 6607    	mla	r6, r0, r7, r6
    ... omitted non-call instructions ...
      62: 4631         	mov	r1, r6
      64: f7ff fffe    	bl	0x64 <SRNOS_KernelRunLoop+0x64> @ imm = #-0x4
			00000064:  R_ARM_THM_CALL	SRNOS_CycleBegin
      68: 6821         	ldr	r1, [r4]
      6a: 68a0         	ldr	r0, [r4, #0x8]
      6c: 4788         	blx	r1
      6e: 4628         	mov	r0, r5
      70: f7ff fffe    	bl	0x70 <SRNOS_KernelRunLoop+0x70> @ imm = #-0x4
			00000070:  R_ARM_THM_CALL	SRNOS_CycleEnd
      74: 2700         	movs	r7, #0x0
      76: e7d2         	b	0x1e <SRNOS_KernelRunLoop+0x1e> @ imm = #-0x5c
      78: b00b         	add	sp, #0x2c
      7a: bdf0         	pop	{r4, r5, r6, r7, pc}
      7c: 4802         	ldr	r0, [pc, #0x8]          @ 0x88 <SRNOS_KernelRunLoop+0x88>
      7e: 4903         	ldr	r1, [pc, #0xc]          @ 0x8c <SRNOS_KernelRunLoop+0x8c>
      80: f240 1227    	movw	r2, #0x127
      84: f7ff fffe    	bl	0x84 <SRNOS_KernelRunLoop+0x84> @ imm = #-0x4
			00000084:  R_ARM_THM_CALL	SRNOS_KernelFailure
      88: 07 00 00 00  	.word	0x00000007
			00000088:  R_ARM_ABS32	.rodata.str1.1

