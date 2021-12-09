#include "ns.h"
#include "inc/lib.h"

extern union Nsipc nsipcbuf;

static struct jif_pkt *pkt = (struct jif_pkt*)REQVA;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";
	int r;

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	if ((r = sys_page_alloc(0, pkt, PTE_P|PTE_U|PTE_W)) < 0)
			panic("sys_page_alloc: %e", r);
	while (1) {
		int length;
		do {
			length = sys_net_receive(pkt->jp_data);
			if (length == -E_NO_MEM) {
				sys_yield();
			}
		} while(length == -E_NO_MEM);
		if (length < 0) {
			cprintf("receive fail code %d from %08x\n", length);
		}
		pkt->jp_len = length;
		ipc_send(ns_envid, NSREQ_INPUT, pkt, PTE_P|PTE_U);
		sys_yield();
		sys_yield();
	}
}
