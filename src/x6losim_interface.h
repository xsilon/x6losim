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

enum msg_type
{
	MSG_TYPE_REG_REQ = 0,
	MSG_TYPE_REG_CON,
	MSG_TYPE_DEREG_REQ,
	MSG_TYPE_DEREG_CON,
	MSG_TYPE_CCA_REQ,
	MSG_TYPE_CCA_CON,
	MSG_TYPE_TX_REQ,
	MSG_TYPE_TX_CON,
	MSG_TYPE_TX_DONE_IND,
};

#pragma pack (push, 1)

struct netsim_pkt_hdr
{
	/* Includes size of this header */
	uint16_t len;
	/* One of the enum msg_type values */
	uint16_t msg_type;
	/* Used to check NetSim and Node are using the same structure */
	uint32_t interface_version;
	/* For initial request this contains the assigned node id */
	uint64_t node_id;
	/* 16 bit checksum of complete msg where this field is 0 */
	uint16_t cksum;
} __attribute__((__packed__ ));

/*
 * NetSim -> Node Registration Request
 *
 * Sent after accepting the TCP connection from a node, it informs the node
 * of it's unique 64 bit node ID which the node must use in all corresponding
 * communication including the response to this request.
 */
struct netsim_to_node_registration_req_pkt
{
	struct netsim_pkt_hdr hdr;
} __attribute__((__packed__ ));


/*
 * Node -> NetSim Registration Confirm.
 *
 * Confirms the node's registration, the node will send back information on
 * it's current setup.
 */
struct node_to_netsim_registration_con_pkt
{
	struct netsim_pkt_hdr hdr;

	char os[32];
	char os_version[32];

} __attribute__((__packed__ ));

/*
 * Node -> NetSim Clear Channel Assessment Request.
 *
 * A node will send this if it's using CCA mode 1 or 3 to ask if the channel
 * is clear.
 */
struct node_to_netsim_cca_req_pkt
{
	struct netsim_pkt_hdr hdr;

} __attribute__((__packed__ ));

/*
 * Node -> NetSim DeRegistration Request.
 *
 * Request node's deregistration.
 */
struct node_to_netsim_deregistration_req_pkt
{
	struct netsim_pkt_hdr hdr;

} __attribute__((__packed__ ));

/*
 * We have a 128 byte header followed by the actual 802.15.4 frame.
 */
struct node_to_netsim_data_pkt_hdr
{
	union data_union
	{
		struct data_hdr {
			struct netsim_pkt_hdr hdr;

			uint8_t source_addr[8];
			uint16_t psdu_len; /* length of the actual 802.15.4 frame after this hdr */
			uint8_t rep_code; /* the repitition code it was sent at */
			int8_t tx_power; /* the power it was sent at */
			uint8_t cca_mode; /* the cca mode used */
			int8_t rssi; /* Received signal strength, set by simulator */
		} hdr;
		struct data_all {
			char hdr_area[NETSIM_PKT_HDR_SZ];
			char data[NETSIM_PKT_DATA_SZ];
		} all;

	};
} __attribute__((__packed__ ));

#pragma pack (pop)

#endif
