/* Define the flash max size */
__max_flash_size = 768k;

__base = 0x10000000;

__comm_vma = 0x11000;
__max_comm_size = 32k;


MEMORY
{
    init            : org = __base,             len = 512
    flash(rx)       : org = __base + 512,       len = __max_flash_size
    comm(rx)        : org = __comm_vma,         len = __max_comm_size
    data            : org = 0x19000,            len = 20k
    stack           : org = 0x1E000,            len = 8k
    aram            : org = 0x50000,            len = 16k
    dram            : org = 0x58000,            len = 16k
    cram            : org = 0x20000,            len = 16k
}

SECTIONS
{
    .init : {
        *(.reset)
    } > init

    .comm : {
        *(.vector)
        *(.com_text*)
        *(.com_rodata*)
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        *(.rela*)
        *(.data*)
	    *(.sdata*)
        LONG(0)
    } > comm AT > flash

    .flash : {

        . = ALIGN(512);
    } > flash

    .bss (NOLOAD):
    {
        __bss_start = .;
        *(.bss)
        *(.sbss)
        *(.buf*)
        __bss_end = .;
    } > data
    __bss_size = __bss_end - __bss_start;

    .stack (NOLOAD) : {
        __irq_stack_start = .;
        . = 0x2000;
        __irq_stack = .;
    } > stack
    __irq_stack_size = __irq_stack - __irq_stack_start;

    .code_aecram (NOLOAD): {
        __aram_start = .;
    } > aram

    .code_decram (NOLOAD): {
    } > dram
}

/* Calc the lma */
__bank_size = SIZEOF(.flash);
__comm_lma = LOADADDR(.comm);
__comm_size = SIZEOF(.comm);
