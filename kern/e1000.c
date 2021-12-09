#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

// LAB 6: Your driver code here

#define TDARRAY_SIZE 8
#define RDARRAY_SIZE 128

volatile void *e1000_base = 0;
struct tx_desc TXDarray[TDARRAY_SIZE] = {0};
char tx_buffer[TDARRAY_SIZE * 1518] = {0};

struct rx_desc RXDarray[RDARRAY_SIZE] = {0};
char rx_buffer[RDARRAY_SIZE * 2048] = {0};

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
        TXDarray[i].addr = PADDR((tx_buffer + i * 1518));
        TXDarray[i].status |= 1;
        //TXDarray[i].length = 1518;
    }

    write_reg(E1000_TDBAH, 0);
    write_reg(E1000_TDBAL, PADDR(TXDarray));
    write_reg(E1000_TDLEN, sizeof(TXDarray));
    write_reg(E1000_TDH, 0);
    write_reg(E1000_TDT, 0);
    uint32_t tctl = read_reg(E1000_TCTL);
    tctl |= E1000_TCTL_EN;
    tctl |= E1000_TCTL_PSP;
    tctl |= 0x40000; // E1000_TCTL_COLD
    write_reg(E1000_TCTL, tctl);
    write_reg(E1000_TIPG, 0xa + (4 << 10) + (6 << 20));
    /*
    TXDarray[0].status &= (~1);
    strcpy(tx_buffer,"I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!I'm here!");
    TXDarray[0].cmd |= 8; // E1000_TXD_CMD_RS
    TXDarray[0].cmd |= 1; // E1000_TXD_CMD_EOP
    write_reg(E1000_TDT, 1);
    assert(read_reg(E1000_TDH) == 1);
    assert(TXDarray[0].status & 1);
    */
    write_reg(E1000_RA, 0x12005452);
    write_reg(E1000_RA + 4, 0x5634 | E1000_RAH_AV);
    for (size_t i = E1000_MTA; i< E1000_RA; i += 4) {
        write_reg(i, 0);
    }
    for (int i = 0;i < RDARRAY_SIZE; ++i) {
        RXDarray[i].addr = PADDR((rx_buffer + i * 2048));
        RXDarray[i].status = 0;
    }
    write_reg(E1000_RDBAH, 0);
    write_reg(E1000_RDBAL, PADDR(RXDarray));
    write_reg(E1000_RDLEN, sizeof(RXDarray));
    write_reg(E1000_RDH, 0);
    write_reg(E1000_RDT, RDARRAY_SIZE - 1);
    size_t rtcl = E1000_RCTL_EN;
    rtcl |= E1000_RCTL_SECRC;
    rtcl |= E1000_RCTL_BAM;
    write_reg(E1000_RCTL, rtcl);
    return 1;
}

int 
transmit_packet(void *src, size_t length) {
    int tail;
    
    assert(length < 1518);
    tail = read_reg(E1000_TDT);
    if (!(TXDarray[tail].status & 1)) {
        cprintf("TXDarray full tail %d\n", tail);
        return -E_NO_MEM;
    }
    memcpy(tx_buffer + 1518 * tail , src, length);
    TXDarray[tail].cmd |= 8; // E1000_TXD_CMD_RS
    TXDarray[tail].cmd |= 1; // E1000_TXD_CMD_EOP
    TXDarray[tail].status &= (~1);
    TXDarray[tail].length = length;
    //cprintf("send tail %d length %d %.*s\n", tail, TXDarray[tail].length, length, tx_buffer + 1518 * tail);
    write_reg(E1000_TDT, (tail + 1) % TDARRAY_SIZE);
    //assert(TXDarray[tail].status & 1);
    return 0;
}

int 
receive_packet(void *dst) {
    int tail;

    tail = (read_reg(E1000_RDT) + 1) % RDARRAY_SIZE;
    if (!(RXDarray[tail].status & 1)) {
        //cprintf("RXDarray empty tail %d\n", tail);
        return -E_NO_MEM;
    }
    //cprintf("receive tail %d length %d\n", tail, RXDarray[tail].length);
    RXDarray[tail].status &= (~1);
    memcpy(dst, rx_buffer + 2048 * tail, RXDarray[tail].length);
    write_reg(E1000_RDT, tail);
    return RXDarray[tail].length;
}