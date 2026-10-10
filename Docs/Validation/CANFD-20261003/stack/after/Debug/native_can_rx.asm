00000000 <SRNOS_NativeCANRxRouterWorker>:
       0: e92d 41f0    	push.w	{r4, r5, r6, r7, r8, lr}
       4: b0fa         	sub	sp, #0x1e8
       6: 4604         	mov	r4, r0
       8: f88d 01bb    	strb.w	r0, [sp, #0x1bb]
       c: b2c0         	uxtb	r0, r0
       e: f7ff fffe    	bl	0xe <SRNOS_NativeCANRxRouterWorker+0xe> @ imm = #-0x4
			0000000e:  R_ARM_THM_CALL	SRNOS_NativeCANTaskCtxOf
      12: 4605         	mov	r5, r0
      14: b370         	cbz	r0, 0x74 <SRNOS_NativeCANRxRouterWorker+0x74> @ imm = #0x5c
      16: f89d 01bb    	ldrb.w	r0, [sp, #0x1bb]
      1a: f7ff fffe    	bl	0x1a <SRNOS_NativeCANRxRouterWorker+0x1a> @ imm = #-0x4
			0000001a:  R_ARM_THM_CALL	SRNOS_NativeCANBackendEnsureBound
      1e: f7ff fffe    	bl	0x1e <SRNOS_NativeCANRxRouterWorker+0x1e> @ imm = #-0x4
			0000001e:  R_ARM_THM_CALL	xTaskGetCurrentTaskHandle
      22: f3bf 8f5f    	dmb	sy
      26: 6028         	str	r0, [r5]
      28: f89d 01bb    	ldrb.w	r0, [sp, #0x1bb]
      2c: f7ff fffe    	bl	0x2c <SRNOS_NativeCANRxRouterWorker+0x2c> @ imm = #-0x4
			0000002c:  R_ARM_THM_CALL	SRNOS_FDCANStart
      30: a977         	add	r1, sp, #0x1dc
      32: 9174         	str	r1, [sp, #0x1d0]
    ... omitted non-call instructions ...
      6c: 9676         	str	r6, [sp, #0x1d8]
      6e: f7ff fffe    	bl	0x6e <SRNOS_NativeCANRxRouterWorker+0x6e> @ imm = #-0x4
			0000006e:  R_ARM_THM_CALL	SRNOS_KernelRunLoop
      72: 9801         	ldr	r0, [sp, #0x4]
      74: f89d 01bb    	ldrb.w	r0, [sp, #0x1bb]
      78: b07a         	add	sp, #0x1e8
      7a: e8bd 81f0    	pop.w	{r4, r5, r6, r7, r8, pc}

Disassembly of section .text._ZZL18SRNOS_KernelRunCppIZ29SRNOS_NativeCANRxRouterWorkerE3$_1Z29SRNOS_NativeCANRxRouterWorkerE3$_0EvT_T0_jbENUlPvE_8__invokeES4_:

00000000 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)>:
       0: e92d 4ff0    	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
       4: b095         	sub	sp, #0x54
       6: 6807         	ldr	r7, [r0]
       8: e9d7 0100    	ldrd	r0, r1, [r7]
       c: 6804         	ldr	r4, [r0]
       e: 7808         	ldrb	r0, [r1]
      10: f36f 045e    	bfc	r4, #1, #30
      14: f7ff fffe    	bl	0x14 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x14> @ imm = #-0x4
			00000014:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      18: 687a         	ldr	r2, [r7, #0x4]
      1a: 4601         	mov	r1, r0
    ... omitted non-call instructions ...
      2c: d002         	beq	0x34 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x34> @ imm = #0x4
      2e: f7ff fffe    	bl	0x2e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x2e> @ imm = #-0x4
			0000002e:  R_ARM_THM_CALL	SRNOS_CANFDCoreNoteRxWake
      32: e001         	b	0x38 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x38> @ imm = #0x2
      34: f7ff fffe    	bl	0x34 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x34> @ imm = #-0x4
			00000034:  R_ARM_THM_CALL	SRNOS_CANRouterCoreNoteRxWake
      38: a803         	add	r0, sp, #0xc
      3a: 2600         	movs	r6, #0x0
    ... omitted non-call instructions ...
      6c: 465a         	mov	r2, r11
      6e: f7ff fffe    	bl	0x6e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x6e> @ imm = #-0x4
			0000006e:  R_ARM_THM_CALL	SRNOS_FDCANRxPop
      72: 2800         	cmp	r0, #0x0
      74: 9002         	str	r0, [sp, #0x8]
    ... omitted non-call instructions ...
      86: bf00         	nop
      88: f7ff fffe    	bl	0x88 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x88> @ imm = #-0x4
			00000088:  R_ARM_THM_CALL	SRNOS_CANRouterCoreNoteRxRejected
      8c: f1ba 0a01    	subs.w	r10, r10, #0x1
      90: f109 096c    	add.w	r9, r9, #0x6c
    ... omitted non-call instructions ...
      a0: 4628         	mov	r0, r5
      a2: f7ff fffe    	bl	0xa2 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xa2> @ imm = #-0x4
			000000a2:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      a6: f8d8 100c    	ldr.w	r1, [r8, #0xc]
      aa: 4604         	mov	r4, r0
    ... omitted non-call instructions ...
      b0: 4608         	mov	r0, r1
      b2: f7ff fffe    	bl	0xb2 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xb2> @ imm = #-0x4
			000000b2:  R_ARM_THM_CALL	SRNOS_CANFDDlcToLength
      b6: e000         	b	0xba <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xba> @ imm = #0x0
      b8: 20ff         	movs	r0, #0xff
    ... omitted non-call instructions ...
      d0: d0da         	beq	0x88 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x88> @ imm = #-0x4c
      d2: f7ff fffe    	bl	0xd2 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xd2> @ imm = #-0x4
			000000d2:  R_ARM_THM_CALL	SRNOS_CANFDCoreNoteRxRejected
      d6: e7d9         	b	0x8c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x8c> @ imm = #-0x4e
      d8: f8d8 1008    	ldr.w	r1, [r8, #0x8]
    ... omitted non-call instructions ...
      f8: a803         	add	r0, sp, #0xc
      fa: f7ff fffe    	bl	0xfa <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xfa> @ imm = #-0x4
			000000fa:  R_ARM_THM_CALL	__aeabi_memclr4
      fe: f8d8 0000    	ldr.w	r0, [r8]
     102: 9003         	str	r0, [sp, #0xc]
    ... omitted non-call instructions ...
     152: 9801         	ldr	r0, [sp, #0x4]
     154: f7ff fffe    	bl	0x154 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x154> @ imm = #-0x4
			00000154:  R_ARM_THM_CALL	__aeabi_memcpy4
     158: f7ff fffe    	bl	0x158 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x158> @ imm = #-0x4
			00000158:  R_ARM_THM_CALL	SRNOS_NativeCANBackendNowMs
     15c: 4602         	mov	r2, r0
     15e: 4628         	mov	r0, r5
     160: a903         	add	r1, sp, #0xc
     162: f7ff fffe    	bl	0x162 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x162> @ imm = #-0x4
			00000162:  R_ARM_THM_CALL	SRNOS_CANFDCoreOnRxFrame
     166: e791         	b	0x8c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x8c> @ imm = #-0xde
     168: 2100         	movs	r1, #0x0
    ... omitted non-call instructions ...
     19a: 9801         	ldr	r0, [sp, #0x4]
     19c: f7ff fffe    	bl	0x19c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x19c> @ imm = #-0x4
			0000019c:  R_ARM_THM_CALL	__aeabi_memcpy4
     1a0: f7ff fffe    	bl	0x1a0 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x1a0> @ imm = #-0x4
			000001a0:  R_ARM_THM_CALL	SRNOS_NativeCANBackendNowMs
     1a4: 4602         	mov	r2, r0
     1a6: 4628         	mov	r0, r5
     1a8: a903         	add	r1, sp, #0xc
     1aa: f7ff fffe    	bl	0x1aa <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x1aa> @ imm = #-0x4
			000001aa:  R_ARM_THM_CALL	SRNOS_CANRouterCoreOnRxFrame
     1ae: e76d         	b	0x8c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x8c> @ imm = #-0x126
     1b0: 2e20         	cmp	r6, #0x20
    ... omitted non-call instructions ...
     1b6: 7800         	ldrb	r0, [r0]
     1b8: f7ff fffe    	bl	0x1b8 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x1b8> @ imm = #-0x4
			000001b8:  R_ARM_THM_CALL	SRNOS_NativeCANBackendSignalRx
     1bc: f7ff fffe    	bl	0x1bc <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x1bc> @ imm = #-0x4
			000001bc:  R_ARM_THM_CALL	osThreadYield
     1c0: b015         	add	sp, #0x54
     1c2: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}

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

