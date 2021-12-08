#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

// LAB 6: Your driver code here

#define TDARRAY_SIZE 8

volatile void *e1000_base = 0;
struct tx_desc TDarray[TDARRAY_SIZE] = {0};
char tx_buffer[TDARRAY_SIZE * 1518] = {0};

inline static void 
write_reg(int reg, uint32_t value) {
    assert(e1000_base);
    *(uint32_t*)(e1000_base + reg) = value;
}

inline static uint32_t 
read_reg(int reg) {
    assert(e1000_base);
    return *(uint32_t*)(e1000_base + reg);
}

int 
pci_e1000_attach(struct pci_func *pcif) 
{
    pci_func_enable(pcif);
    cprintf("pci_e1000_attach reg_base[0] %x reg_size[0] %x\n", pcif->reg_base[0], pcif->reg_size[0]);
    e1000_base = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

    assert(read_reg(E1000_STATUS) == 0x80080783);
    
    for (int i = 0;i < TDARRAY_SIZE; ++i) {
        TDarray[i].addr = PADDR((tx_buffer + i * 1518));
        TDarray[i].status |= 1;
        //TDarray[i].length = 1518;
    }

    write_reg(E1000_TDBAH, 0);
    write_reg(E1000_TDBAL, PADDR(TDarray));
    write_reg(E1000_TDLEN, sizeof(TDarray));
    write_reg(E1000_TDH, 0);
    write_reg(E1000_TDT, 0);
    uint32_t tctl = read_reg(E1000_TCTL);
    tctl |= E1000_TCTL_EN;
    tctl |= E1000_TCTL_PSP;
    tctl |= 0x40000; // E1000_TCTL_COLD
    write_reg(E1000_TCTL, tctl);
    write_reg(E1000_TIPG, 0xa + (4 << 10) + (6 << 20));
    /*
    TDarray[0].status &= (~1);
    strcpy(tx_buffer,"I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!");
    TDarray[0].cmd |= 8; // E1000_TXD_CMD_RS
    TDarray[0].cmd |= 1; // E1000_TXD_CMD_EOP
    write_reg(E1000_TDT, 1);
    assert(read_reg(E1000_TDH) == 1);
    assert(TDarray[0].status & 1);
    */
    return 1;
}

int 
transmit_packet(void *src, size_t length) {
    int tail;
    
    assert(length < 1518);
    tail = read_reg(E1000_TDT);
    if (!(TDarray[tail].status & 1)) {
        cprintf("TDarray full tail %d\n", tail);
        return -E_NO_MEM;
    }
    memcpy(tx_buffer + 1518 * tail , src, length);
    TDarray[tail].cmd |= 8; // E1000_TXD_CMD_RS
    TDarray[tail].cmd |= 1; // E1000_TXD_CMD_EOP
    TDarray[tail].status &= (~1);
    TDarray[tail].length = length;
    cprintf("send tail %d length %d %.*s\n", tail, TDarray[tail].length, length, tx_buffer + 1518 * tail);
    write_reg(E1000_TDT, (tail + 1) % TDARRAY_SIZE);
    //assert(TDarray[tail].status & 1);
    return 0;
}
