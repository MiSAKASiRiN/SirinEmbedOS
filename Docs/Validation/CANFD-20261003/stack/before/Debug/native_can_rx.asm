00000000 <SRNOS_NativeCANRxRouterWorker>:
       0: e92d 41f0    	push.w	{r4, r5, r6, r7, r8, lr}
       4: f6ad 5db8    	subw	sp, sp, #0xdb8
       8: 4604         	mov	r4, r0
       a: f88d 0d8b    	strb.w	r0, [sp, #0xd8b]
       e: b2c0         	uxtb	r0, r0
      10: f7ff fffe    	bl	0x10 <SRNOS_NativeCANRxRouterWorker+0x10> @ imm = #-0x4
			00000010:  R_ARM_THM_CALL	SRNOS_NativeCANTaskCtxOf
      14: 4605         	mov	r5, r0
      16: b3d8         	cbz	r0, 0x90 <SRNOS_NativeCANRxRouterWorker+0x90> @ imm = #0x76
      18: f89d 0d8b    	ldrb.w	r0, [sp, #0xd8b]
      1c: f7ff fffe    	bl	0x1c <SRNOS_NativeCANRxRouterWorker+0x1c> @ imm = #-0x4
			0000001c:  R_ARM_THM_CALL	SRNOS_NativeCANBackendEnsureBound
      20: f7ff fffe    	bl	0x20 <SRNOS_NativeCANRxRouterWorker+0x20> @ imm = #-0x4
			00000020:  R_ARM_THM_CALL	xTaskGetCurrentTaskHandle
      24: f3bf 8f5f    	dmb	sy
      28: 6028         	str	r0, [r5]
      2a: f89d 0d8b    	ldrb.w	r0, [sp, #0xd8b]
      2e: f7ff fffe    	bl	0x2e <SRNOS_NativeCANRxRouterWorker+0x2e> @ imm = #-0x4
			0000002e:  R_ARM_THM_CALL	SRNOS_FDCANStart
      32: f60d 51ac    	addw	r1, sp, #0xdac
      36: f8cd 1da0    	str.w	r1, [sp, #0xda0]
    ... omitted non-call instructions ...
      86: f8cd 1d94    	str.w	r1, [sp, #0xd94]
      8a: f7ff fffe    	bl	0x8a <SRNOS_NativeCANRxRouterWorker+0x8a> @ imm = #-0x4
			0000008a:  R_ARM_THM_CALL	SRNOS_KernelRunLoop
      8e: 9801         	ldr	r0, [sp, #0x4]
      90: f89d 0d8b    	ldrb.w	r0, [sp, #0xd8b]
      94: f60d 5db8    	addw	sp, sp, #0xdb8
      98: e8bd 81f0    	pop.w	{r4, r5, r6, r7, r8, pc}

Disassembly of section .text._ZZL18SRNOS_KernelRunCppIZ29SRNOS_NativeCANRxRouterWorkerE3$_1Z29SRNOS_NativeCANRxRouterWorkerE3$_0EvT_T0_jbENUlPvE_8__invokeES4_:

00000000 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)>:
       0: e92d 4ff0    	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
       4: b093         	sub	sp, #0x4c
       6: 6805         	ldr	r5, [r0]
       8: e9d5 0100    	ldrd	r0, r1, [r5]
       c: 6804         	ldr	r4, [r0]
       e: 7808         	ldrb	r0, [r1]
      10: f36f 045e    	bfc	r4, #1, #30
      14: f7ff fffe    	bl	0x14 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x14> @ imm = #-0x4
			00000014:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      18: 686a         	ldr	r2, [r5, #0x4]
      1a: 4601         	mov	r1, r0
    ... omitted non-call instructions ...
      26: 0949         	lsrs	r1, r1, #0x5
      28: f7ff fffe    	bl	0x28 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x28> @ imm = #-0x4
			00000028:  R_ARM_THM_CALL	SRNOS_CANFDCoreNoteRxWake
      2c: e005         	b	0x3a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x3a> @ imm = #0xa
      2e: 1e61         	subs	r1, r4, #0x1
    ... omitted non-call instructions ...
      34: 0949         	lsrs	r1, r1, #0x5
      36: f7ff fffe    	bl	0x36 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x36> @ imm = #-0x4
			00000036:  R_ARM_THM_CALL	SRNOS_CANRouterCoreNoteRxWake
      3a: e9d5 0101    	ldrd	r0, r1, [r5, #4]
      3e: 2220         	movs	r2, #0x20
      40: 7800         	ldrb	r0, [r0]
      42: f7ff fffe    	bl	0x42 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x42> @ imm = #-0x4
			00000042:  R_ARM_THM_CALL	SRNOS_FDCANRxPop
      46: 2800         	cmp	r0, #0x0
      48: f000 80aa    	beq.w	0x1a0 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x1a0> @ imm = #0x154
    ... omitted non-call instructions ...
      5e: bf00         	nop
      60: f7ff fffe    	bl	0x60 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x60> @ imm = #-0x4
			00000060:  R_ARM_THM_CALL	SRNOS_CANRouterCoreNoteRxRejected
      64: f1bb 0b01    	subs.w	r11, r11, #0x1
      68: f104 046c    	add.w	r4, r4, #0x6c
    ... omitted non-call instructions ...
      7a: 4638         	mov	r0, r7
      7c: f7ff fffe    	bl	0x7c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x7c> @ imm = #-0x4
			0000007c:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      80: f8d9 100c    	ldr.w	r1, [r9, #0xc]
      84: 4606         	mov	r6, r0
    ... omitted non-call instructions ...
      8a: 4608         	mov	r0, r1
      8c: f7ff fffe    	bl	0x8c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x8c> @ imm = #-0x4
			0000008c:  R_ARM_THM_CALL	SRNOS_CANFDDlcToLength
      90: e003         	b	0x9a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x9a> @ imm = #0x6
      92: bf00         	nop
    ... omitted non-call instructions ...
      b0: d0d6         	beq	0x60 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x60> @ imm = #-0x54
      b2: f7ff fffe    	bl	0xb2 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xb2> @ imm = #-0x4
			000000b2:  R_ARM_THM_CALL	SRNOS_CANFDCoreNoteRxRejected
      b6: e7d5         	b	0x64 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x64> @ imm = #-0x56
      b8: f8d9 1008    	ldr.w	r1, [r9, #0x8]
    ... omitted non-call instructions ...
      d8: a801         	add	r0, sp, #0x4
      da: f7ff fffe    	bl	0xda <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xda> @ imm = #-0x4
			000000da:  R_ARM_THM_CALL	__aeabi_memclr4
      de: f8d9 0000    	ldr.w	r0, [r9]
      e2: 9001         	str	r0, [sp, #0x4]
    ... omitted non-call instructions ...
     132: 9800         	ldr	r0, [sp]
     134: f7ff fffe    	bl	0x134 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x134> @ imm = #-0x4
			00000134:  R_ARM_THM_CALL	__aeabi_memcpy4
     138: f7ff fffe    	bl	0x138 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x138> @ imm = #-0x4
			00000138:  R_ARM_THM_CALL	SRNOS_NativeCANBackendNowMs
     13c: 4602         	mov	r2, r0
     13e: 4638         	mov	r0, r7
     140: a901         	add	r1, sp, #0x4
     142: f7ff fffe    	bl	0x142 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x142> @ imm = #-0x4
			00000142:  R_ARM_THM_CALL	SRNOS_CANFDCoreOnRxFrame
     146: e78d         	b	0x64 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x64> @ imm = #-0xe6
     148: e9cd aa03    	strd	r10, r10, [sp, #12]
    ... omitted non-call instructions ...
     178: 9800         	ldr	r0, [sp]
     17a: f7ff fffe    	bl	0x17a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x17a> @ imm = #-0x4
			0000017a:  R_ARM_THM_CALL	__aeabi_memcpy4
     17e: f7ff fffe    	bl	0x17e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x17e> @ imm = #-0x4
			0000017e:  R_ARM_THM_CALL	SRNOS_NativeCANBackendNowMs
     182: 4602         	mov	r2, r0
     184: 4638         	mov	r0, r7
     186: a901         	add	r1, sp, #0x4
     188: f7ff fffe    	bl	0x188 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x188> @ imm = #-0x4
			00000188:  R_ARM_THM_CALL	SRNOS_CANRouterCoreOnRxFrame
     18c: e76a         	b	0x64 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x64> @ imm = #-0x12c
     18e: f1b8 0f20    	cmp.w	r8, #0x20
    ... omitted non-call instructions ...
     196: 7800         	ldrb	r0, [r0]
     198: f7ff fffe    	bl	0x198 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x198> @ imm = #-0x4
			00000198:  R_ARM_THM_CALL	SRNOS_NativeCANBackendSignalRx
     19c: f7ff fffe    	bl	0x19c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x19c> @ imm = #-0x4
			0000019c:  R_ARM_THM_CALL	osThreadYield
     1a0: b013         	add	sp, #0x4c
     1a2: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}

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

