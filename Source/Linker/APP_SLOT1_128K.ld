ENTRY(Reset_Handler)

_estack = 0x20005000;
_Min_Heap_Size = 0x400;
_Min_Stack_Size = 0x800;

MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
  FLASH (rx)  : ORIGIN = 0x08002000, LENGTH = 56K
}

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

  /* Image header - placed immediately after vector table */
  /* Address must match BOOT_APP_BASE_ADDR + BOOT_IMAGE_HEADER_OFFSET */
  .image_header :
  {
    . = ALIGN(4);
    KEEP(*(.image_header))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text) *(.text*) *(.rodata) *(.rodata*)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
  .bss : { . = ALIGN(4); _sbss = .; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  ._user_heap_stack : { . = ALIGN(8); end = .; _end = end; . = . + _Min_Heap_Size; . = . + _Min_Stack_Size; } > RAM
}
