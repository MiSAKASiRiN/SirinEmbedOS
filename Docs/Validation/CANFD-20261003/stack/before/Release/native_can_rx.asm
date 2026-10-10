00000000 <SRNOS_NativeCANRxRouterWorker>:
       0: b5b0         	push	{r4, r5, r7, lr}
       2: f6ad 5db8    	subw	sp, sp, #0xdb8
       6: b2c4         	uxtb	r4, r0
       8: f88d 0d8b    	strb.w	r0, [sp, #0xd8b]
       c: 4620         	mov	r0, r4
       e: f7ff fffe    	bl	0xe <SRNOS_NativeCANRxRouterWorker+0xe> @ imm = #-0x4
			0000000e:  R_ARM_THM_CALL	SRNOS_NativeCANTaskCtxOf
      12: b3c8         	cbz	r0, 0x88 <SRNOS_NativeCANRxRouterWorker+0x88> @ imm = #0x72
      14: 4605         	mov	r5, r0
      16: 4620         	mov	r0, r4
      18: f7ff fffe    	bl	0x18 <SRNOS_NativeCANRxRouterWorker+0x18> @ imm = #-0x4
			00000018:  R_ARM_THM_CALL	SRNOS_NativeCANBackendEnsureBound
      1c: f7ff fffe    	bl	0x1c <SRNOS_NativeCANRxRouterWorker+0x1c> @ imm = #-0x4
			0000001c:  R_ARM_THM_CALL	xTaskGetCurrentTaskHandle
      20: f3bf 8f5f    	dmb	sy
      24: 6028         	str	r0, [r5]
      26: 4620         	mov	r0, r4
      28: f7ff fffe    	bl	0x28 <SRNOS_NativeCANRxRouterWorker+0x28> @ imm = #-0x4
			00000028:  R_ARM_THM_CALL	SRNOS_FDCANStart
      2c: a901         	add	r1, sp, #0x4
      2e: f8cd 1dac    	str.w	r1, [sp, #0xdac]
    ... omitted non-call instructions ...
      80: f8cd 1d94    	str.w	r1, [sp, #0xd94]
      84: f7ff fffe    	bl	0x84 <SRNOS_NativeCANRxRouterWorker+0x84> @ imm = #-0x4
			00000084:  R_ARM_THM_CALL	SRNOS_KernelRunLoop
      88: f60d 5db8    	addw	sp, sp, #0xdb8
      8c: bdb0         	pop	{r4, r5, r7, pc}

Disassembly of section .text._ZZL18SRNOS_KernelRunCppIZ29SRNOS_NativeCANRxRouterWorkerE3$_1Z29SRNOS_NativeCANRxRouterWorkerE3$_0EvT_T0_jbENUlPvE_8__invokeES4_:

00000000 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)>:
       0: e92d 4ff0    	push.w	{r4, r5, r6, r7, r8, r9, r10, r11, lr}
       4: b093         	sub	sp, #0x4c
       6: 6806         	ldr	r6, [r0]
       8: e9d6 0100    	ldrd	r0, r1, [r6]
       c: 6804         	ldr	r4, [r0]
       e: 7808         	ldrb	r0, [r1]
      10: f36f 045e    	bfc	r4, #1, #30
      14: f7ff fffe    	bl	0x14 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x14> @ imm = #-0x4
			00000014:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      18: 6872         	ldr	r2, [r6, #0x4]
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
      3a: e9d6 0101    	ldrd	r0, r1, [r6, #4]
      3e: 2220         	movs	r2, #0x20
      40: 7800         	ldrb	r0, [r0]
      42: f7ff fffe    	bl	0x42 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x42> @ imm = #-0x4
			00000042:  R_ARM_THM_CALL	SRNOS_FDCANRxPop
      46: 2800         	cmp	r0, #0x0
      48: f000 809b    	beq.w	0x182 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x182> @ imm = #0x136
    ... omitted non-call instructions ...
      5e: e006         	b	0x6e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x6e> @ imm = #0xc
      60: f7ff fffe    	bl	0x60 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x60> @ imm = #-0x4
			00000060:  R_ARM_THM_CALL	SRNOS_CANRouterCoreNoteRxRejected
      64: 3c01         	subs	r4, #0x1
      66: f10b 0b6c    	add.w	r11, r11, #0x6c
    ... omitted non-call instructions ...
      78: 4638         	mov	r0, r7
      7a: f7ff fffe    	bl	0x7a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x7a> @ imm = #-0x4
			0000007a:  R_ARM_THM_CALL	SRNOS_CANControllerIsFD
      7e: f8d9 100c    	ldr.w	r1, [r9, #0xc]
      82: 4605         	mov	r5, r0
    ... omitted non-call instructions ...
      88: 4608         	mov	r0, r1
      8a: f7ff fffe    	bl	0x8a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x8a> @ imm = #-0x4
			0000008a:  R_ARM_THM_CALL	SRNOS_CANFDDlcToLength
      8e: 2100         	movs	r1, #0x0
      90: 2809         	cmp	r0, #0x9
    ... omitted non-call instructions ...
      a4: d0dc         	beq	0x60 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x60> @ imm = #-0x48
      a6: f7ff fffe    	bl	0xa6 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xa6> @ imm = #-0x4
			000000a6:  R_ARM_THM_CALL	SRNOS_CANFDCoreNoteRxRejected
      aa: e7db         	b	0x64 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x64> @ imm = #-0x4a
      ac: f8d9 1008    	ldr.w	r1, [r9, #0x8]
    ... omitted non-call instructions ...
      cc: 4650         	mov	r0, r10
      ce: f7ff fffe    	bl	0xce <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0xce> @ imm = #-0x4
			000000ce:  R_ARM_THM_CALL	__aeabi_memclr4
      d2: f8d9 0000    	ldr.w	r0, [r9]
      d6: 9001         	str	r0, [sp, #0x4]
    ... omitted non-call instructions ...
     11c: 9800         	ldr	r0, [sp]
     11e: f7ff fffe    	bl	0x11e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x11e> @ imm = #-0x4
			0000011e:  R_ARM_THM_CALL	__aeabi_memcpy4
     122: f7ff fffe    	bl	0x122 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x122> @ imm = #-0x4
			00000122:  R_ARM_THM_CALL	SRNOS_NativeCANBackendNowMs
     126: 4602         	mov	r2, r0
     128: 4638         	mov	r0, r7
     12a: a901         	add	r1, sp, #0x4
     12c: f7ff fffe    	bl	0x12c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x12c> @ imm = #-0x4
			0000012c:  R_ARM_THM_CALL	SRNOS_CANRouterCoreOnRxFrame
     130: e798         	b	0x64 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x64> @ imm = #-0xd0
     132: 2000         	movs	r0, #0x0
    ... omitted non-call instructions ...
     15a: 9800         	ldr	r0, [sp]
     15c: f7ff fffe    	bl	0x15c <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x15c> @ imm = #-0x4
			0000015c:  R_ARM_THM_CALL	__aeabi_memcpy4
     160: f7ff fffe    	bl	0x160 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x160> @ imm = #-0x4
			00000160:  R_ARM_THM_CALL	SRNOS_NativeCANBackendNowMs
     164: 4602         	mov	r2, r0
     166: 4638         	mov	r0, r7
     168: a901         	add	r1, sp, #0x4
     16a: f7ff fffe    	bl	0x16a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x16a> @ imm = #-0x4
			0000016a:  R_ARM_THM_CALL	SRNOS_CANFDCoreOnRxFrame
     16e: e779         	b	0x64 <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x64> @ imm = #-0x10e
     170: f1b8 0f20    	cmp.w	r8, #0x20
    ... omitted non-call instructions ...
     178: 7800         	ldrb	r0, [r0]
     17a: f7ff fffe    	bl	0x17a <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x17a> @ imm = #-0x4
			0000017a:  R_ARM_THM_CALL	SRNOS_NativeCANBackendSignalRx
     17e: f7ff fffe    	bl	0x17e <void SRNOS_KernelRunCpp<SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0>(SRNOS_NativeCANRxRouterWorker::$_1, SRNOS_NativeCANRxRouterWorker::$_0, unsigned int, bool)::'lambda'(void*)::__invoke(void*)+0x17e> @ imm = #-0x4
			0000017e:  R_ARM_THM_CALL	osThreadYield
     182: b013         	add	sp, #0x4c
     184: e8bd 8ff0    	pop.w	{r4, r5, r6, r7, r8, r9, r10, r11, pc}

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

