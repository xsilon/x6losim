/* _____________________________________________________________________ x6losim

    Xsilon Network Simulator using QEMU Virtual 802.15.4 Nodes.

    This header file defines the interface between x6losim and the QEMU
    virtual nodes.

    Martin Townsend
        email: martin.townsend@xsilon.com
        email: mtownsend1973@gmail.com
        skype: mtownsend1973
    All Rights Reserved Xsilon Ltd 2014.
 */

#ifndef _INC_6LOSIM_INTERFACE
#define _INC_6LOSIM_INTERFACE

#include <stdint.h>

#define NETSIM_PKT_HDR_SZ				(128)

/*
 * We have a 128 byte header followed by the actual 802.15.4 frame.
 */
struct netsim_pkt_hdr
{
	uint8_t source_addr[8];
	uint16_t psdu_len; /* length of the actual 802.15.4 frame after this hdr */
	uint8_t rep_code; /* the repitition code it was sent at */
	int8_t tx_power; /* the power it was sent at */
	uint8_t cca_mode; /* the cca mode used */
	int8_t rssi; /* Received signal strength, set by simulator */
};


#endif
