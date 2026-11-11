/* STM32G474RET6: 512 KB flash, 128 KB RAM in three blocks.
 *
 *   SRAM1   80 KB at 0x2000 0000
 *   SRAM2   16 KB at 0x2001 4000   (directly after SRAM1: one 96 KB region)
 *   CCM     32 KB at 0x1000 0000
 *
 * Data, bss and the stack use the 96 KB of SRAM1 + SRAM2. The CCM block is declared and left empty:
 * the skeleton uses about 3 KB of RAM. (ST's own script for this chip treats all 128 KB as one region
 * through the CCM alias at 0x2001 8000; that is not relied on here.)
 *
 * Symbols used by ST's startup_stm32g474xx.s: _estack, _sidata, _sdata, _edata, _sbss, _ebss. */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH  (rx) : ORIGIN = 0x08000000, LENGTH = 512K
    RAM    (rw) : ORIGIN = 0x20000000, LENGTH = 96K
    CCMRAM (rw) : ORIGIN = 0x10000000, LENGTH = 32K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack = 0x1000; /* the link fails if data + bss leave less than this */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    /* reserves nothing at run time: the stack grows down from _estack. It only makes the link fail
     * when less than _min_stack is left. */
    .stack_reserve (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + _min_stack;
        . = ALIGN(8);
    } > RAM

    /DISCARD/ :
    {
        *(.eh_frame*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
