// 32bit code to Power On Self Test (POST) a machine.
//
// Copyright (C) 2008  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ioport.h" // PORT_*
#include "config.h" // CONFIG_*
#include "cmos.h" // CMOS_*
#include "util.h" // memset
#include "biosvar.h" // struct bios_data_area_s
#include "ata.h" // hard_drive_setup
#include "disk.h" // floppy_drive_setup
#include "memmap.h" // add_e820
#include "pic.h" // pic_setup
#include "pci.h" // create_pirtable
#include "acpi.h" // acpi_bios_init
#include "bregs.h" // struct bregs

#define bda ((struct bios_data_area_s *)MAKE_FARPTR(SEG_BDA, 0))
#define ebda ((struct extended_bios_data_area_s *)MAKE_FARPTR(SEG_EBDA, 0))

static void
set_irq(int vector, void *loc)
{
    SET_BDA(ivecs[vector].seg, SEG_BIOS);
    SET_BDA(ivecs[vector].offset, (u32)loc - BUILD_BIOS_ADDR);
}

// Symbols defined in romlayout.S
extern void dummy_iret_handler();
extern void entry_08();
extern void entry_09();
extern void entry_hwirq();
extern void entry_0e();
extern void entry_10();
extern void entry_11();
extern void entry_12();
extern void entry_13();
extern void entry_14();
extern void entry_15();
extern void entry_16();
extern void entry_17();
extern void entry_18();
extern void entry_19();
extern void entry_1a();
extern void entry_1c();
extern void entry_40();
extern void entry_70();
extern void entry_74();
extern void entry_75();
extern void entry_76();

static void
init_bda()
{
    dprintf(3, "init bda\n");
    memset(bda, 0, sizeof(*bda));

    SET_BDA(mem_size_kb, BASE_MEM_IN_K);

    int i;
    for (i=0; i<256; i++)
        set_irq(i, &dummy_iret_handler);

    set_irq(0x08, &entry_08);
    set_irq(0x09, &entry_09);
    //set_irq(0x0a, &entry_hwirq);
    //set_irq(0x0b, &entry_hwirq);
    //set_irq(0x0c, &entry_hwirq);
    //set_irq(0x0d, &entry_hwirq);
    set_irq(0x0e, &entry_0e);
    //set_irq(0x0f, &entry_hwirq);
    set_irq(0x10, &entry_10);
    set_irq(0x11, &entry_11);
    set_irq(0x12, &entry_12);
    set_irq(0x13, &entry_13);
    set_irq(0x14, &entry_14);
    set_irq(0x15, &entry_15);
    set_irq(0x16, &entry_16);
    set_irq(0x17, &entry_17);
    set_irq(0x18, &entry_18);
    set_irq(0x19, &entry_19);
    set_irq(0x1a, &entry_1a);
    set_irq(0x1c, &entry_1c);
    set_irq(0x40, &entry_40);
    set_irq(0x70, &entry_70);
    //set_irq(0x71, &entry_hwirq);
    //set_irq(0x72, &entry_hwirq);
    //set_irq(0x73, &entry_hwirq);
    set_irq(0x74, &entry_74);
    set_irq(0x75, &entry_75);
    set_irq(0x76, &entry_76);
    //set_irq(0x77, &entry_hwirq);

    // set vector 0x79 to zero
    // this is used by 'gardian angel' protection system
    SET_BDA(ivecs[0x79].seg, 0);
    SET_BDA(ivecs[0x79].offset, 0);

    set_irq(0x1E, &diskette_param_table2);
}

static void
init_ebda()
{
    memset(ebda, 0, sizeof(*ebda));
    ebda->size = EBDA_SIZE;
    SET_BDA(ebda_seg, SEG_EBDA);
    SET_BDA(ivecs[0x41].seg, SEG_EBDA);
    SET_BDA(ivecs[0x41].offset
            , offsetof(struct extended_bios_data_area_s, fdpt[0]));
    SET_BDA(ivecs[0x46].seg, SEG_EBDA);
    SET_BDA(ivecs[0x41].offset
            , offsetof(struct extended_bios_data_area_s, fdpt[1]));
}

static void
ram_probe(void)
{
    dprintf(3, "Find memory size\n");
    if (CONFIG_COREBOOT) {
        coreboot_fill_map();
    } else {
        // On emulators, get memory size from nvram.
        u32 rs = ((inb_cmos(CMOS_MEM_EXTMEM2_LOW) << 16)
                  | (inb_cmos(CMOS_MEM_EXTMEM2_HIGH) << 24));
        if (rs)
            rs += 16 * 1024 * 1024;
        else
            rs = (((inb_cmos(CMOS_MEM_EXTMEM_LOW) << 10)
                   | (inb_cmos(CMOS_MEM_EXTMEM_HIGH) << 18))
                  + 1 * 1024 * 1024);
        SET_EBDA(ram_size, rs);
        add_e820(0, rs, E820_RAM);

        // Check for memory over 4Gig
        u64 high = ((inb_cmos(CMOS_MEM_HIGHMEM_LOW) << 16)
                    | (inb_cmos(CMOS_MEM_HIGHMEM_MID) << 24)
                    | ((u64)inb_cmos(CMOS_MEM_HIGHMEM_HIGH) << 32));
        SET_EBDA(ram_size_over4G, high);
        add_e820(0x100000000ull, high, E820_RAM);

        /* reserve 256KB BIOS area at the end of 4 GB */
        add_e820(0xfffc0000, 256*1024, E820_RESERVED);
    }

    // Don't declare any memory between 0xa0000 and 0x100000
    add_e820(0xa0000, 0x50000, E820_HOLE);

    // Mark known areas as reserved.
    add_e820((u32)MAKE_FARPTR(SEG_EBDA, 0), EBDA_SIZE * 1024, E820_RESERVED);
    add_e820(BUILD_BIOS_ADDR, BUILD_BIOS_SIZE, E820_RESERVED);

    dprintf(1, "ram_size=0x%08x\n", GET_EBDA(ram_size));
}

static void
init_bios_tables(void)
{
    if (CONFIG_COREBOOT)
        // XXX - not supported on coreboot yet.
        return;

    smm_init();

    create_pirtable();

    mptable_init();

    smbios_init();

    acpi_bios_init();
}

static void
init_boot_vectors()
{
    if (! CONFIG_BOOT)
        return;
    dprintf(3, "init boot device ordering\n");

    // Floppy drive
    struct ipl_entry_s *ip = &ebda->ipl.table[0];
    ip->type = IPL_TYPE_FLOPPY;
    ip++;

    // First HDD
    ip->type = IPL_TYPE_HARDDISK;
    ip++;

    // CDROM
    if (CONFIG_CDROM_BOOT) {
        ip->type = IPL_TYPE_CDROM;
        ip++;
    }

    ebda->ipl.count = ip - ebda->ipl.table;
    ebda->ipl.sequence = 0xffff;
    if (CONFIG_COREBOOT) {
        // XXX - hardcode defaults for coreboot.
        ebda->ipl.bootorder = 0x00000231;
        ebda->ipl.checkfloppysig = 1;
    } else {
        // On emulators, get boot order from nvram.
        ebda->ipl.bootorder = (inb_cmos(CMOS_BIOS_BOOTFLAG2)
                               | ((inb_cmos(CMOS_BIOS_BOOTFLAG1) & 0xf0) << 4));
        if (!(inb_cmos(CMOS_BIOS_BOOTFLAG1) & 1))
            ebda->ipl.checkfloppysig = 1;
    }
}

// Main setup code.
static void
post()
{
    init_bda();
    init_ebda();

    pic_setup();
    timer_setup();
    mathcp_setup();

    vga_setup();

    kbd_setup();
    lpt_setup();
    serial_setup();
    mouse_setup();

    memmap_setup();
    ram_probe();
    pci_bios_setup();
    init_bios_tables();
    memmap_finalize();

    floppy_drive_setup();
    hard_drive_setup();

    init_boot_vectors();

    optionrom_setup();
}

// Clear .bss section for C code.
static void
clear_bss()
{
    dprintf(3, "clearing .bss section\n");
    extern char __bss_start[], __bss_end[];
    memset(__bss_start, 0, __bss_end - __bss_start);
}

// Reset DMA controller
static void
init_dma()
{
    // first reset the DMA controllers
    outb(0, PORT_DMA1_MASTER_CLEAR);
    outb(0, PORT_DMA2_MASTER_CLEAR);

    // then initialize the DMA controllers
    outb(0xc0, PORT_DMA2_MODE_REG);
    outb(0x00, PORT_DMA2_MASK_REG);
}

// Check if the machine was setup with a special restart vector.
static void
check_restart_status()
{
    // Get and then clear CMOS shutdown status.
    u8 status = inb_cmos(CMOS_RESET_CODE);
    outb_cmos(0, CMOS_RESET_CODE);

    if (status == 0x00 || status == 0x09 || status >= 0x0d)
        // Normal post
        return;

    if (status != 0x05) {
        BX_PANIC("Unimplemented shutdown status: %02x\n", status);
        return;
    }

    // XXX - this is supposed to jump without changing any memory -
    // but the stack has been altered by the time the code gets here.
    eoi_pic2();
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.cs = GET_BDA(jump_cs_ip) >> 16;
    br.ip = GET_BDA(jump_cs_ip);
    call16(&br);
}

// 32-bit entry point.
void VISIBLE32
_start()
{
    init_dma();
    check_restart_status();

    debug_serial_setup();
    dprintf(1, "Start bios\n");

    // Setup for .bss and .data sections
    make_bios_writable();
    clear_bss();

    // Perform main setup code.
    post();

    // Present the user with a bootup menu.
    interactive_bootmenu();

    // Setup bios checksum.
    extern char bios_checksum;
    bios_checksum = -checksum((u8*)BUILD_BIOS_ADDR, BUILD_BIOS_SIZE - 1);

    // Prep for boot process.
    make_bios_readonly();

    // Invoke int 19 to start boot process.
    dprintf(3, "Jump to int19\n");
    struct bregs br;
    memset(&br, 0, sizeof(br));
    call16_int(0x19, &br);
}

// Ughh - some older gcc compilers have a bug which causes VISIBLE32
// functions to not be exported as a global variable - force _start
// to be global here.
asm(".global _start");
