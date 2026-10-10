00000000 <SRNOS_NativeCANRxRouterWorker>:
       0: b5b0         	push	{r4, r5, r7, lr}
       2: b0fa         	sub	sp, #0x1e8
       4: b2c4         	uxtb	r4, r0
       6: f88d 01bb    	strb.w	r0, [sp, #0x1bb]
       a: 4620         	mov	r0, r4
       c: f7ff fffe    	bl	0xc <SRNOS_NativeCANRxRouterWorker+0xc> @ imm = #-0x4
			0000000c:  R_ARM_THM_CALL	SRNOS_NativeCANTaskCtxOf
      10: b358         	cbz	r0, 0x6a <SRNOS_NativeCANRxRouterWorker+0x6a> @ imm = #0x56
      12: 4605         	mov	r5, r0
      14: 4620         	mov	r0, r4
      16: f7ff fffe    	bl	0x16 <SRNOS_NativeCANRxRouterWorker+0x16> @ imm = #-0x4
			00000016:  R_ARM_THM_CALL	SRNOS_NativeCANBackendEnsureBound
      1a: f7ff fffe    	bl	0x1a <SRNOS_NativeCANRxRouterWorker+0x1a> @ imm = #-0x4
			0000001a:  R_ARM_THM_CALL	xTaskGetCurrentTaskHandle
      1e: f3bf 8f5f    	dmb	sy
      22: 6028         	str	r0, [r5]
      24: 4620         	mov	r0, r4
      26: f7ff fffe    	bl	0x26 <SRNOS_NativeCANRxRouterWorker+0x26> @ imm = #-0x4
			00000026:  R_ARM_THM_CALL	SRNOS_FDCANStart
      2a: a901         	add	r1, sp, #0x4
      2c: 9177         	str	r1, [sp, #0x1dc]
    ... omitted non-call instructions ...
      64: 9279         	str	r2, [sp, #0x1e4]
      66: f7ff fffe    	bl	0x66 <SRNOS_NativeCANRxRouterWorker+0x66> @ imm = #-0x4
			00000066:  R_ARM_THM_CALL	SRNOS_KernelRunLoop
      6a: b07a         	add	sp, #0x1e8
      6c: bdb0         	pop	{r4, r5, r7, pc}

Disassembly of section .text._ZZL18SRNOS_KernelRunCppIZ29SRNOS_NativeCANRxRouterWorkerE3$_1Z29SRNOS_NativeCANRxRouterWorkerE3$_0EvT_T0_jbENUlPvE_8__invokeES4_:

00000000 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)>:
       0: e92d 4ff0    	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
       4: b095         	sub	sp, #0x54
       6: 6804         	ldr	r4, [r0]
       8: e9d4 0100    	ldrd	r0, r1, [r4]
       c: 6805         	ldr	r5, [r0]
       e: 7808         	ldrb	r0, [r1]
      10: f36f 055e    	bfc	r5, #1, #30
      14: f7ff fffe    	bl	0x14 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x14> @ imm = #-0x4
			00000014:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      18: 6862         	ldr	r2, [r4, #0x4]
      1a: 4601         	mov	r1, r0
    ... omitted non-call instructions ...
      2c: d002         	beq	0x34 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x34> @ imm = #0x4
      2e: f7ff fffe    	bl	0x2e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x2e> @ imm = #-0x4
			0000002e:  R_ARM_THM_CALL	SRNOS_CANFDCoreNoteRxWake
      32: e001         	b	0x38 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x38> @ imm = #0x2
      34: f7ff fffe    	bl	0x34 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x34> @ imm = #-0x4
			00000034:  R_ARM_THM_CALL	SRNOS_CANRouterCoreNoteRxWake
      38: a803         	add	r0, sp, #0xc
      3a: f04f 0800    	mov.w	r8, #0x0
    ... omitted non-call instructions ...
      6a: 463a         	mov	r2, r7
      6c: f7ff fffe    	bl	0x6c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x6c> @ imm = #-0x4
			0000006c:  R_ARM_THM_CALL	SRNOS_FDCANRxPop
      70: 2800         	cmp	r0, #0x0
      72: 9002         	str	r0, [sp, #0x8]
    ... omitted non-call instructions ...
      7e: e006         	b	0x8e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x8e> @ imm = #0xc
      80: f7ff fffe    	bl	0x80 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x80> @ imm = #-0x4
			00000080:  R_ARM_THM_CALL	SRNOS_CANRouterCoreNoteRxRejected
      84: f1bb 0b01    	subs.w	r11, r11, #0x1
      88: f109 096c    	add.w	r9, r9, #0x6c
    ... omitted non-call instructions ...
      98: 4630         	mov	r0, r6
      9a: f7ff fffe    	bl	0x9a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x9a> @ imm = #-0x4
			0000009a:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      9e: f8da 100c    	ldr.w	r1, [r10, #0xc]
      a2: 4605         	mov	r5, r0
    ... omitted non-call instructions ...
      a8: 4608         	mov	r0, r1
      aa: f7ff fffe    	bl	0xaa <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xaa> @ imm = #-0x4
			000000aa:  R_ARM_THM_CALL	SRNOS_CANFDDlcToLength
      ae: 2100         	movs	r1, #0x0
      b0: 2809         	cmp	r0, #0x9
    ... omitted non-call instructions ...
      c4: d0dc         	beq	0x80 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x80> @ imm = #-0x48
      c6: f7ff fffe    	bl	0xc6 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xc6> @ imm = #-0x4
			000000c6:  R_ARM_THM_CALL	SRNOS_CANFDCoreNoteRxRejected
      ca: e7db         	b	0x84 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x84> @ imm = #-0x4a
      cc: f8da 1008    	ldr.w	r1, [r10, #0x8]
    ... omitted non-call instructions ...
      ec: 9801         	ldr	r0, [sp, #0x4]
      ee: f7ff fffe    	bl	0xee <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xee> @ imm = #-0x4
			000000ee:  R_ARM_THM_CALL	__aeabi_memclr4
      f2: f8da 0000    	ldr.w	r0, [r10]
      f6: 9003         	str	r0, [sp, #0xc]
    ... omitted non-call instructions ...
     13c: 9800         	ldr	r0, [sp]
     13e: f7ff fffe    	bl	0x13e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x13e> @ imm = #-0x4
			0000013e:  R_ARM_THM_CALL	__aeabi_memcpy4
     142: f7ff fffe    	bl	0x142 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x142> @ imm = #-0x4
			00000142:  R_ARM_THM_CALL	SRNOS_NativeCANBackendNowMs
     146: 4602         	mov	r2, r0
     148: 4630         	mov	r0, r6
     14a: a903         	add	r1, sp, #0xc
     14c: f7ff fffe    	bl	0x14c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x14c> @ imm = #-0x4
			0000014c:  R_ARM_THM_CALL	SRNOS_CANRouterCoreOnRxFrame
     150: e798         	b	0x84 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x84> @ imm = #-0xd0
     152: 2000         	movs	r0, #0x0
    ... omitted non-call instructions ...
     17a: 9800         	ldr	r0, [sp]
     17c: f7ff fffe    	bl	0x17c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x17c> @ imm = #-0x4
			0000017c:  R_ARM_THM_CALL	__aeabi_memcpy4
     180: f7ff fffe    	bl	0x180 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x180> @ imm = #-0x4
			00000180:  R_ARM_THM_CALL	SRNOS_NativeCANBackendNowMs
     184: 4602         	mov	r2, r0
     186: 4630         	mov	r0, r6
     188: a903         	add	r1, sp, #0xc
     18a: f7ff fffe    	bl	0x18a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x18a> @ imm = #-0x4
			0000018a:  R_ARM_THM_CALL	SRNOS_CANFDCoreOnRxFrame
     18e: e779         	b	0x84 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x84> @ imm = #-0x10e
     190: f1b8 0f20    	cmp.w	r8, #0x20
    ... omitted non-call instructions ...
     198: 7800         	ldrb	r0, [r0]
     19a: f7ff fffe    	bl	0x19a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x19a> @ imm = #-0x4
			0000019a:  R_ARM_THM_CALL	SRNOS_NativeCANBackendSignalRx
     19e: f7ff fffe    	bl	0x19e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x19e> @ imm = #-0x4
			0000019e:  R_ARM_THM_CALL	osThreadYield
     1a2: b015         	add	sp, #0x54
     1a4: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}

Disassembly of section .text._ZZL18SRNOS_KernelRunCppIZ29SRNOS_NativeCANRxRouterWorkerE3$_1Z29SRNOS_NativeCANRxRouterWorkerE3$_0EvT_T0_jbENUlPvE0_8__invokeES4_:

00000000 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda0'(void*)::__invoke(void*)>:
       0: b580         	push	{r7, lr}
       2: b082         	sub	sp, #0x8
       4: 6840         	ldr	r0, [r0, #0x4]
       6: 2100         	movs	r1, #0x0
       8: f04f 32ff    	mov.w	r2, #0xffffffff
       c: f04f 3cff    	mov.w	r12, #0xffffffff
      10: 6803         	ldr	r3, [r0]
      12: 2000         	movs	r0, #0x0
      14: f8cd c000    	str.w	r12, [sp]
      18: f7ff fffe    	bl	0x18 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda0'(void*)::__invoke(void*)+0x18> @ imm = #-0x4
			00000018:  R_ARM_THM_CALL	xTaskGenericNotifyWait
      1c: 2001         	movs	r0, #0x1
      1e: b002         	add	sp, #0x8
      20: bd80         	pop	{r7, pc}

