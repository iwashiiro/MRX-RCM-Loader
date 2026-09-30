OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
    . = LOAD_ADDR;
    .text : { *(.text) }
    . = ALIGN(4);
    /DISCARD/ : { *(.data*) *(.bss*) *(.comment*) *(.note*) }
}
