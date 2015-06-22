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
#define NETSIM_PKT_DATA_SZ				(128)
#define NETSIM_PKT_MAX_SZ				(256)
#define NETSIM_INTERFACE_VERSION			(0x00000001)

#define HANADU_NODE_PORT				(11555)
#define WIRELESS_NODE_PORT				(11556)

struct node_to_netsim_pkt_hdr
{
	uint32_t len;
	/* Used to check NetSim and Node are using the same structure */
	uint32_t interface_version;
	uint64_t node_id;

} __attribute__((__packed__ ));

struct netsim_to_node_pkt_hdr
{
	uint32_t len;
	/* Used to check NetSim and Node are using the same structure */
	uint32_t interface_version;
	/* For initial request this contain the assigned node id */
	uint64_t node_id;
};

/*
 * NetSim -> Node Registration Request
 *
 * Sent after accepting the TCP connection from a node, it informs the node
 * of it's unique 64 bit node ID which the node must use in all corresponding
 * communication including the response to this request.
 */
struct netsim_to_node_registration_req_pkt
{
	netsim_to_node_pkt_hdr hdr;
} __attribute__((__packed__ ));


/*
 * Node -> NetSim Registration Confirm.
 *
 * Confirms the node's registration, the node will send back information on
 * it's current setup.
 */
struct node_to_netsim_registration_con_pkt
{
	node_to_netsim_pkt_hdr hdr;

	char os[32];
	char os_version[32];

} __attribute__((__packed__ ));


/*
 * We have a 128 byte header followed by the actual 802.15.4 frame.
 */
struct node_to_netsim_data_pkt_hdr
{
	union
	{
		struct {
			struct node_to_netsim_pkt_hdr hdr;

			uint8_t source_addr[8];
			uint16_t psdu_len; /* length of the actual 802.15.4 frame after this hdr */
			uint8_t rep_code; /* the repitition code it was sent at */
			int8_t tx_power; /* the power it was sent at */
			uint8_t cca_mode; /* the cca mode used */
			int8_t rssi; /* Received signal strength, set by simulator */
		};
		struct {
			char hdr_area[NETSIM_PKT_HDR_SZ];
			char data[NETSIM_PKT_DATA_SZ];
		};

	};
} __attribute__((__packed__ ));


#endif
