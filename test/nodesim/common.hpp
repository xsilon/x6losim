#ifndef _INC_NODESIM_COMMON_HPP
#define _INC_NODESIM_COMMON_HPP

#include <byteswap.h>
#include <stdint.h>

#ifndef ntohll
#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t ntohll(uint64_t x) {
	return bswap_64(x);
}
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t ntohll(uint64_t x)
{
	return x;
}
#endif
#endif
#ifndef htonll
#define htonll ntohll
#endif


uint16_t
generate_checksum(void *msg, int msglen);

class NodeSim_pimpl;
class NodeSim
{
public:
	NodeSim();
	virtual ~NodeSim();

	void connectNetSim();

	void sendRegCon();
	void sendDeregReq();
	void sendCcaReq();
	void sendTxDataInd();

	void readMsg();
private:
	NodeSim_pimpl *pimpl;
};



#endif
