00000000 <SRNOS_CANFDCoreOnRxFrame>:
       0: e92d 4ff0    	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
       4: b093         	sub	sp, #0x4c
       6: 4604         	mov	r4, r0
       8: 1e46         	subs	r6, r0, #0x1
       a: f240 0800    	movw	r8, #0x0
			0000000a:  R_ARM_THM_MOVW_ABS_NC	(anonymous namespace)::contexts
       e: f241 60c4    	movw	r0, #0x16c4
      12: f2c0 0800    	movt	r8, #0x0
			00000012:  R_ARM_THM_MOVT_ABS	(anonymous namespace)::contexts
      16: 1f62         	subs	r2, r4, #0x5
      18: fb16 8b00    	smlabb	r11, r6, r0, r8
    ... omitted non-call instructions ...
      24: 2700         	movlo	r7, #0x0
      26: d202         	bhs	0x2e <SRNOS_CANFDCoreOnRxFrame+0x2e> @ imm = #0x4
      28: b013         	add	sp, #0x4c
      2a: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}
      2e: b331         	cbz	r1, 0x7e <SRNOS_CANFDCoreOnRxFrame+0x7e> @ imm = #0x4c
      30: f240 0000    	movw	r0, #0x0
    ... omitted non-call instructions ...
      4a: 460d         	mov	r5, r1
      4c: f7ff fffe    	bl	0x4c <SRNOS_CANFDCoreOnRxFrame+0x4c> @ imm = #-0x4
			0000004c:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      50: b1a8         	cbz	r0, 0x7e <SRNOS_CANFDCoreOnRxFrame+0x7e> @ imm = #0x2a
      52: 7968         	ldrb	r0, [r5, #0x5]
    ... omitted non-call instructions ...
      5a: 7908         	ldrb	r0, [r1, #0x4]
      5c: f7ff fffe    	bl	0x5c <SRNOS_CANFDCoreOnRxFrame+0x5c> @ imm = #-0x4
			0000005c:  R_ARM_THM_CALL	SRNOS_CANFDLengthToDlc
      60: 28ff         	cmp	r0, #0xff
      62: d00c         	beq	0x7e <SRNOS_CANFDCoreOnRxFrame+0x7e> @ imm = #0x18
    ... omitted non-call instructions ...
      a2: f3bf 8f5f    	dmb	sy
      a6: f380 8811    	msr	basepri, r0
      aa: b013         	add	sp, #0x4c
      ac: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}
      b0: f241 6084    	movw	r0, #0x1684
      b4: f10d 0804    	add.w	r8, sp, #0x4
    ... omitted non-call instructions ...
      c2: 2144         	movs	r1, #0x44
      c4: f7ff fffe    	bl	0xc4 <SRNOS_CANFDCoreOnRxFrame+0xc4> @ imm = #-0x4
			000000c4:  R_ARM_THM_CALL	__aeabi_memclr4
      c8: 7932         	ldrb	r2, [r6, #0x4]
      ca: f108 0008    	add.w	r0, r8, #0x8
    ... omitted non-call instructions ...
      da: f88d 5009    	strb.w	r5, [sp, #0x9]
      de: f7ff fffe    	bl	0xde <SRNOS_CANFDCoreOnRxFrame+0xde> @ imm = #-0x4
			000000de:  R_ARM_THM_CALL	__aeabi_memcpy4
      e2: f44f 50b4    	mov.w	r0, #0x1680
      e6: f81b c000    	ldrb.w	r12, [r11, r0]
    ... omitted non-call instructions ...
     152: 4620         	mov	r0, r4
     154: 4659         	mov	r1, r11
     156: 4798         	blx	r3
     158: 3701         	adds	r7, #0x1
     15a: 7828         	ldrb	r0, [r5]
    ... omitted non-call instructions ...
     190: f3bf 8f5f    	dmb	sy
     194: f380 8811    	msr	basepri, r0
     198: b013         	add	sp, #0x4c
     19a: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}

Disassembly of section .text.SRNOS_CANFDCoreNextTxWait:

