ENTRY(_start)

PHDRS
{
    text    PT_LOAD;
    data    PT_LOAD;
    bss     PT_LOAD;
}

SECTIONS
{
    . = CONSTANT(MAXPAGESIZE);
    TEXT_BEGIN = .;
    
    .text : {
        *(.text.*)
        *(.rodata .rodata.*)
    } : text
    
    .ctors : {
        CTORS_BEGIN = .;
        KEEP(*(.ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.init_array))
        CTORS_END = .;
    } : text
    
    .dtors : {
        DTORS_BEGIN = .;
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors.*))
        DTORS_END = .;
    } : text
    
    .eh_frame_hdr : {
        *(.eh_frame_hdr)
    } : text
    
    TEXT_END = .;
    
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    
    DATA_BEGIN = .;
    
    .data : {
        __profile_table_start = .;
        KEEP(*(.profile));
        __profile_table_end = .;
        __param_table_start = .;
        KEEP(*(.param));
        __param_table_end = .;
        
        *(.data .data.*)
    } : data
    
    .eh_frame : {
        EH_FRAME_BEGIN = .;
        KEEP(*(.eh_frame))
        LONG(0)
    } : data

    .gcc_except_table : {
        KEEP(*(.gcc_except_table))
        KEEP(*(.gcc_except_table.*))
    } : data
    
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    
    .bss : {
        *(.bss .bss.*)
    } : bss
    
    DATA_END = .;
}
