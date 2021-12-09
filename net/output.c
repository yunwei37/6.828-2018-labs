#include "ns.h"
#include "inc/lib.h"

extern union Nsipc nsipcbuf;

#define debug 0

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";
	struct jif_pkt* nsipcreq = (struct jif_pkt *)REQVA;

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	uint32_t req, whom;
	int perm, r;

	while (1) {
		perm = 0;
		req = ipc_recv((int32_t *) &whom, nsipcreq, &perm);
		if (debug)
			cprintf("output req %d from %08x [page %08x: %s]\n",
				req, whom, uvpt[PGNUM(nsipcreq)], nsipcreq);
		
		// All requests must contain an argument page
		if (!(perm & PTE_P)) {
			cprintf("Invalid request from %08x: no argument page\n",
				whom);
			continue; // just leave it hanging...
		}

		if (req == NSREQ_OUTPUT) {
			do {
				r = sys_net_transmit(nsipcreq->jp_data, nsipcreq->jp_len);
				if (r != 0) {
					sys_yield();
				}
			} while(r == -E_NO_MEM);
		} else {
			cprintf("Invalid request code %d from %08x\n", req, whom);
			r = -E_INVAL;
		}
		sys_page_unmap(0, nsipcreq);
	}
}
