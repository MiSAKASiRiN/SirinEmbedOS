/*
******************************************************************************
**

**  File        : LinkerScript.ld
**
**  Author		: STM32CubeMX
**
**  Abstract    : Linker script for STM32H723VGHx series
**                1024Kbytes FLASH and 560Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed “as is,” without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** <h2><center>&copy; COPYRIGHT(c) 2025 STMicroelectronics</center></h2>
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**   1. Redistributions of source code must retain the above copyright notice,
**      this list of conditions and the following disclaimer.
**   2. Redistributions in binary form must reproduce the above copyright notice,
**      this list of conditions and the following disclaimer in the documentation
**      and/or other materials provided with the distribution.
**   3. Neither the name of STMicroelectronics nor the names of its contributors
**      may be used to endorse or promote products derived from this software
**      without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*****************************************************************************
*/

/* SDK-owned layout derived from CubeMX. Generated baseline remains untouched. */
/* Override capacities with --defsym=SRNOS_MSP_BYTES / SRNOS_LIBC_BYTES. */
PROVIDE(SRNOS_MSP_BYTES = 8192);
PROVIDE(SRNOS_LIBC_BYTES = 8192);

/* Entry Point */
ENTRY(Reset_Handler)

/* Specify the memory areas */
MEMORY
{
DTCMRAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 128K
RAM_D1 (xrw)      : ORIGIN = 0x24000000, LENGTH = 320K
RAM_D2 (xrw)      : ORIGIN = 0x30000000, LENGTH = 32K
RAM_D3 (xrw)      : ORIGIN = 0x38000000, LENGTH = 16K
ITCMRAM (xrw)      : ORIGIN = 0x00000000, LENGTH = 64K
FLASH (rx)      : ORIGIN = 0x8000000, LENGTH = 1024K
}

/* Highest address of the user mode stack */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);    /* end of RAM */
_sstack = _estack - _Min_Stack_Size;
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = SRNOS_LIBC_BYTES;      /* required amount of heap  */
_Min_Stack_Size = SRNOS_MSP_BYTES; /* required amount of stack */

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
  } >RAM_D1 AT> FLASH

 /* Initialized TLS data section */
  .tdata : ALIGN(4)
  {
    *(.tdata .tdata.* .gnu.linkonce.td.*)
    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
    PROVIDE(__data_end = .);
    PROVIDE(__tdata_end = .);
  } >RAM_D1 AT> FLASH

  PROVIDE( __tdata_start = ADDR(.tdata) );
  PROVIDE( __tdata_size = __tdata_end - __tdata_start );

  PROVIDE( __data_start = ADDR(.data) );
  PROVIDE( __data_size = __data_end - __data_start );

  PROVIDE( __tdata_source = LOADADDR(.tdata) );
  PROVIDE( __tdata_source_end = LOADADDR(.tdata) + SIZEOF(.tdata) );
  PROVIDE( __tdata_source_size = __tdata_source_end - __tdata_source );

  PROVIDE( __data_source = LOADADDR(.data) );
  PROVIDE( __data_source_end = __tdata_source_end );
  PROVIDE( __data_source_size = __data_source_end - __data_source );
  /* Uninitialized data section */
  .tbss (NOLOAD) : ALIGN(4)
  {
     /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.tbss .tbss.*)
    . = ALIGN(4);
    PROVIDE( __tbss_end = . );
  } >RAM_D1

  PROVIDE( __tbss_start = ADDR(.tbss) );
  PROVIDE( __tbss_size = __tbss_end - __tbss_start );
  PROVIDE( __tbss_offset = ADDR(.tbss) - ADDR(.tdata) );

  PROVIDE( __tls_base = __tdata_start );
  PROVIDE( __tls_end = __tbss_end );
  PROVIDE( __tls_size = __tls_end - __tls_base );
  PROVIDE( __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)) );
  PROVIDE( __tls_size_align = (__tls_size + __tls_align - 1) & ~(__tls_align - 1) );
  PROVIDE( __arm32_tls_tcb_offset = MAX(8, __tls_align) );
  PROVIDE( __arm64_tls_tcb_offset = MAX(16, __tls_align) );

  .bss (NOLOAD) : ALIGN(4)
  {
    *(.bss)
    *(.bss*)
    *(COMMON)

      . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
      PROVIDE( __bss_end = .);
  } >RAM_D1
  PROVIDE( __non_tls_bss_start = ADDR(.bss) );

  PROVIDE( __bss_start = __tbss_start );
  PROVIDE( __bss_size = __bss_end - __bss_start );

  .sirinos_heap_d1 (NOLOAD) : ALIGN(32)
  {
    __sirinos_heap_d1_start = .;
    KEEP(*(.sirinos_heap_d1))
    __sirinos_heap_d1_end = .;
  } > RAM_D1

  .srnos_libc_heap (NOLOAD) : ALIGN(8)
  {
    __srnos_libc_start = .;
    end = .;
    _end = .;
    . += SRNOS_LIBC_BYTES;
    __srnos_libc_end = .;
  } > RAM_D1

  .sirinos_heap_dtcm (NOLOAD) : ALIGN(32)
  {
    __sirinos_heap_dtcm_start = .;
    __dtcm_heap_start__ = .;
    KEEP(*(.sirinos_heap_dtcm))
    KEEP(*(.dtcm_heap .dtcm_heap.*))
    __sirinos_heap_dtcm_end = .;
    __dtcm_heap_end__ = .;
  } > DTCMRAM

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a:* ( * )
    libm.a:* ( * )
    libgcc.a:* ( * )
  }

}

/* SDK memory sections derived from the prototype DMA fragment. */

/* Place non-cacheable DMA descriptors/buffers.
   NOLOAD: do not bloat image or zero-init in .data/.bss.
   Align to cache line (32B on H7) to avoid DCache aliasing issues. */

SECTIONS
{
  .itcm_text :
  {
    . = ALIGN(32);
    __itcm_text_start__ = .;

    KEEP(*(.itcm_text.keep .itcm_text.keep.*))
    *(.itcm_text .itcm_text.*)

    . = ALIGN(32);
    __itcm_text_end__ = .;
  } > ITCMRAM AT> FLASH

  __itcm_text_load_start__ = LOADADDR(.itcm_text);
  __itcm_text_size__ = __itcm_text_end__ - __itcm_text_start__;

  .dtcm_bss (NOLOAD) :
  {
    . = ALIGN(32);
    __dtcm_bss_start__ = .;

    *(.dtcm_bss .dtcm_bss.*)

    . = ALIGN(32);
    __dtcm_bss_end__ = .;
  } > DTCMRAM

  /* Generic non-cacheable section for descriptors (small structs) */
  .dma_nc (NOLOAD) :
  {
    . = ALIGN(32);
    __dma_nc_start__ = .;

    KEEP(*(.dma_nc .dma_nc.*))    /* user-marked symbols */
    KEEP(*(.RxDescripSection .RxDescripSection.*))
    KEEP(*(.TxDescripSection .TxDescripSection.*))
    *(.dma_nc_static .dma_nc_static.*)

    . = ALIGN(32);
    __dma_nc_end__ = .;
  } > RAM_D2

  /* Optional: bigger ring buffers go here */
  .dma_nc_buf (NOLOAD) :
  {
    . = ALIGN(32);
    __dma_nc_buf_start__ = .;

    *(.dma_nc_buf .dma_nc_buf.*)

    . = ALIGN(32);
    __dma_nc_buf_end__ = .;
  } > RAM_D2

  /* BDMA on STM32H7 D3 peripherals must use SRAM4/RAM_D3 reachable buffers. */
  .bdma_buf (NOLOAD) :
  {
    . = ALIGN(32);
    __bdma_buf_start__ = .;

    *(.bdma_buf .bdma_buf.*)

    . = ALIGN(32);
    __bdma_buf_end__ = .;
  } > RAM_D3

  .eth_dma (NOLOAD) :
  {
    . = ALIGN(32);
    __eth_dma_start__ = .;

    KEEP(*(.eth_dma .eth_dma.*))
    KEEP(*(.RxArraySection .RxArraySection.*))
    KEEP(*(.TxArraySection .TxArraySection.*))

    . = ALIGN(32);
    __eth_dma_end__ = .;
  } > RAM_D2

  /* Debugger-readable watch variables.
     Keep this separate from DMA descriptors so debug state has a clear meaning. */
  .debug_watch (NOLOAD) :
  {
    . = ALIGN(32);
    __debug_watch_start__ = .;

    KEEP(*(.debug_watch .debug_watch.*))

    . = ALIGN(32);
    __debug_watch_end__ = .;
  } > RAM_D2
}

/* Provide handy sizes (not mandatory) */
__dma_nc_size__     = __dma_nc_end__     - __dma_nc_start__;
__dma_nc_buf_size__ = __dma_nc_buf_end__ - __dma_nc_buf_start__;
__bdma_buf_size__   = __bdma_buf_end__   - __bdma_buf_start__;
__eth_dma_size__    = __eth_dma_end__    - __eth_dma_start__;
__debug_watch_size__ = __debug_watch_end__ - __debug_watch_start__;
__dtcm_bss_size__   = __dtcm_bss_end__   - __dtcm_bss_start__;
__dtcm_heap_size__  = __dtcm_heap_end__  - __dtcm_heap_start__;

/* SirinOS retained panic record. The section is intentionally not initialized. */
SECTIONS
{
  .sirinos_noinit (NOLOAD) :
  {
    . = ALIGN(32);
    __sirinos_noinit_start__ = .;
    KEEP(*(.sirinos_noinit .sirinos_noinit.*))
    . = ALIGN(32);
    __sirinos_noinit_end__ = .;
  } > RAM_D2
}

__sirinos_noinit_size__ = __sirinos_noinit_end__ - __sirinos_noinit_start__;


SECTIONS
{
  .srnos_msp _sstack (NOLOAD) :
  {
    __srnos_msp_start = .;
    . += SRNOS_MSP_BYTES;
    __srnos_msp_end = .;
  } > DTCMRAM
}
ASSERT((SRNOS_MSP_BYTES >= 1024) && ((SRNOS_MSP_BYTES & 7) == 0), "Invalid MSP capacity")
ASSERT((SRNOS_LIBC_BYTES >= 256) && ((SRNOS_LIBC_BYTES & 7) == 0), "Invalid libc arena capacity")
ASSERT(__dtcm_bss_end__ <= _sstack, "DTCM data/heap overlaps MSP")
ASSERT(__sirinos_heap_dtcm_end <= _sstack, "FreeRTOS DTCM heap overlaps MSP")
ASSERT(__srnos_libc_end <= ORIGIN(RAM_D1) + LENGTH(RAM_D1), "D1 heap/libc/data overflow")
ASSERT(__sirinos_noinit_end__ <= ORIGIN(RAM_D2) + LENGTH(RAM_D2), "D2 DMA/watch/retained overflow")
ASSERT(__bdma_buf_end__ <= ORIGIN(RAM_D3) + LENGTH(RAM_D3), "BDMA overflow")
ASSERT(__itcm_text_end__ <= ORIGIN(ITCMRAM) + LENGTH(ITCMRAM), "ITCM overflow")
ASSERT((ADDR(.tdata) - ADDR(.data)) == (LOADADDR(.tdata) - LOADADDR(.data)), "TLS data copy must remain contiguous")
