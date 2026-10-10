00000000 <SRNOS_CANFDCoreOnRxFrame>:
       0: e92d 4ff0    	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
       4: b095         	sub	sp, #0x54
       6: 4604         	mov	r4, r0
       8: 3801         	subs	r0, #0x1
       a: f240 0300    	movw	r3, #0x0
			0000000a:  R_ARM_THM_MOVW_ABS_NC	(anonymous namespace)::contexts
       e: f241 67c4    	movw	r7, #0x16c4
      12: f2c0 0300    	movt	r3, #0x0
			00000012:  R_ARM_THM_MOVT_ABS	(anonymous namespace)::contexts
      16: fb10 3707    	smlabb	r7, r0, r7, r3
      1a: 1f60         	subs	r0, r4, #0x5
    ... omitted non-call instructions ...
      48: 4690         	mov	r8, r2
      4a: f7ff fffe    	bl	0x4a <SRNOS_CANFDCoreOnRxFrame+0x4a> @ imm = #-0x4
			0000004a:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      4e: 4642         	mov	r2, r8
      50: b190         	cbz	r0, 0x78 <SRNOS_CANFDCoreOnRxFrame+0x78> @ imm = #0x24
    ... omitted non-call instructions ...
      58: 7928         	ldrb	r0, [r5, #0x4]
      5a: f7ff fffe    	bl	0x5a <SRNOS_CANFDCoreOnRxFrame+0x5a> @ imm = #-0x4
			0000005a:  R_ARM_THM_CALL	SRNOS_CANFDLengthToDlc
      5e: 4642         	mov	r2, r8
      60: 28ff         	cmp	r0, #0xff
    ... omitted non-call instructions ...
      78: 2efc         	cmp	r6, #0xfc
      7a: d202         	bhs	0x82 <SRNOS_CANFDCoreOnRxFrame+0x82> @ imm = #0x4
      7c: b015         	add	sp, #0x54
      7e: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}
      82: 2050         	movs	r0, #0x50
      84: f3ef 8111    	mrs	r1, basepri
    ... omitted non-call instructions ...
      9e: f3bf 8f5f    	dmb	sy
      a2: f381 8811    	msr	basepri, r1
      a6: b015         	add	sp, #0x54
      a8: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}
      ac: f10d 080c    	add.w	r8, sp, #0xc
      b0: 2148         	movs	r1, #0x48
    ... omitted non-call instructions ...
      b6: f50a 56b4    	add.w	r6, r10, #0x1680
      ba: f7ff fffe    	bl	0xba <SRNOS_CANFDCoreOnRxFrame+0xba> @ imm = #-0x4
			000000ba:  R_ARM_THM_CALL	__aeabi_memclr4
      be: 792a         	ldrb	r2, [r5, #0x4]
      c0: 6829         	ldr	r1, [r5]
    ... omitted non-call instructions ...
      d4: f88d 7011    	strb.w	r7, [sp, #0x11]
      d8: f7ff fffe    	bl	0xd8 <SRNOS_CANFDCoreOnRxFrame+0xd8> @ imm = #-0x4
			000000d8:  R_ARM_THM_CALL	__aeabi_memcpy4
      dc: 7830         	ldrb	r0, [r6]
      de: f007 0b01    	and	r11, r7, #0x1
    ... omitted non-call instructions ...
     108: 4620         	mov	r0, r4
     10a: a903         	add	r1, sp, #0xc
     10c: 4798         	blx	r3
     10e: f109 0901    	add.w	r9, r9, #0x1
     112: 7831         	ldrb	r1, [r6]
    ... omitted non-call instructions ...
     19a: 9802         	ldr	r0, [sp, #0x8]
     19c: 9a01         	ldr	r2, [sp, #0x4]
     19e: b015         	add	sp, #0x54
     1a0: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}

Disassembly of section .text.SRNOS_CANFDCoreNextTxWait:

