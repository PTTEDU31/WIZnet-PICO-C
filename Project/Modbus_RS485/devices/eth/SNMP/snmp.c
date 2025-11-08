#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

#include "socket.h"
#include "snmp.h"
#include "snmp_custom.h"

/********************************************************************************************/
/* SNMP : Functions declaration                                                             */
/********************************************************************************************/
// SNMP Parsing functions
int32_t findEntry(uint8_t *oid, int32_t len);
int32_t getOID(int32_t id, uint8_t *oid, uint8_t *len);
int32_t getValue(uint8_t *vptr, int32_t vlen);
int32_t getEntry(int32_t id, uint8_t *dataType, void *ptr, int32_t *len);
int32_t setEntry(int32_t id, void *val, int32_t vlen, uint8_t dataType, int32_t index);
int32_t makeTrapVariableBindings(dataEntryType *oid_data, void *ptr, uint32_t *len);

int32_t parseLength(const uint8_t *msg, int32_t *len);
int32_t parseTLV(const uint8_t *msg, int32_t index, tlvStructType *tlv);
void insertRespLen(int32_t reqStart, int32_t respStart, int32_t size);
int32_t parseVarBind(int32_t reqType, int32_t index);
int32_t parseSequence(int32_t reqType, int32_t index);
int32_t parseSequenceOf(int32_t reqType);
int32_t parseRequest();
int32_t parseCommunity();
int32_t parseVersion();
int32_t parseSNMPMessage();
static int asn1_encode_length(uint8_t *buf, uint32_t len);

// Debugging function
#ifdef _SNMP_DEBUG_
void dumpCode(uint8_t *header, uint8_t *tail, uint8_t *buff, int32_t len);
#endif

// Utils
void ipToByteArray(int8_t *ip, uint8_t *pDes);

/********************************************************************************************/
/* SNMP : Variable declaration                                                              */
/********************************************************************************************/
// SNMP message structures
struct messageStruct request_msg;
struct messageStruct response_msg;

// SNMP Time counter
static time_t startTime = 0;
volatile uint32_t snmp_tick_10ms = 0; // volatile uint32_t snmp_tick_1ms = 0;

// SNMP Sockets
static uint8_t SOCK_SNMP_AGENT;
static uint8_t SOCK_SNMP_TRAP;

uint8_t packet_trap[MAX_TRAPMSG_LEN] = {
	0,
};
uint8_t errorStatus, errorIndex;

/********************************************************************************************/
/* SNMP : Time handler                                                                      */
/********************************************************************************************/
void currentUptime(void *ptr, uint8_t *len)
{
	time_t curTime = getSNMPTimeTick();

	//*(uint32_t *)ptr = (uint32_t)(curTime - startTime) / 10; // calculation for 1ms tick
	*(uint32_t *)ptr = (uint32_t)(curTime - startTime); // calculation for 10ms tick
	*len = 4;
}

void SNMP_time_handler(void)
{
	// snmp_tick_1ms++;
	snmp_tick_10ms++;
}

uint32_t getSNMPTimeTick(void)
{
	// return snmp_tick_1ms;
	return snmp_tick_10ms;
}

/********************************************************************************************/
/* SNMP : Library Part                                                                      */
/********************************************************************************************/
/**
 * @addtogroup snmp_module
 * @{
 */

/**
 * Initialize SNMP Daemon.
 * This should be called just one time at first time
 *
 * @param none
 * @return none
 */
void snmpd_init(uint8_t *managerIP, uint8_t *agentIP, uint8_t sn_agent, uint8_t sn_trap)
{
#ifdef _SNMP_DEBUG_
	printf("\r\n - SNMP : Start SNMP Agent Daemon\r\n");
#endif
	SOCK_SNMP_AGENT = sn_agent;
	SOCK_SNMP_TRAP = sn_trap;

	if ((SOCK_SNMP_AGENT > _WIZCHIP_SOCK_NUM_) || (SOCK_SNMP_TRAP > _WIZCHIP_SOCK_NUM_))
		return;

	startTime = getSNMPTimeTick(); // Start time (unit: 10ms)

	initial_Trap(managerIP, agentIP);

	/*
		// Example Codes for SNMP Trap
		{
			dataEntryType enterprise_oid = {0x0a, {0x2b, 0x06, 0x01, 0x04, 0x01, 0x81, 0x9b, 0x19, 0x01, 0x00},
													SNMPDTYPE_OBJ_ID, 0x0a, {"\x2b\x06\x01\x04\x01\x81\x9b\x19\x10\x00"},	NULL, NULL};

			dataEntryType trap_oid1 = {8, {0x2b, 6, 1, 4, 1, 0, 11, 0}, SNMPDTYPE_OCTET_STRING, 30, {""}, NULL, NULL};
			dataEntryType trap_oid2 = {8, {0x2b, 6, 1, 4, 1, 0, 12, 0}, SNMPDTYPE_INTEGER, 4, {""}, NULL, NULL};

			strcpy((char *)trap_oid1.u.octetstring, "Alert!!!"); 	// String added
			trap_oid2.u.intval = 123456;	// Integer value added

			// Generic Trap: warmStart
			snmp_sendTrap((void *)"192.168.0.214", (void *)"192.168.0.112", (void *)"public", enterprise_oid, SNMPTRAP_WARMSTART, 0, 0);

			// Enterprise-Specific Trap
			snmp_sendTrap((void *)"192.168.0.214", (void *)"192.168.0.112", (void *)"public", enterprise_oid, 6, 0, 2, &trap_oid1, &trap_oid2);
		}
	*/
}

/**
 * SNMP Process Handler.
 * UDP Socket and SNMP Agent transaction handling.
 *
 * @param none
 * @return none
 */

int32_t snmpd_run(void)
{
	int32_t ret;
	int32_t len = 0;

	uint8_t svr_addr[6];
	uint16_t svr_port;
#if 1
	// 20231019 taylor
	uint8_t addr_len;
#endif

	if (SOCK_SNMP_AGENT > _WIZCHIP_SOCK_NUM_)
		return -99;

	switch (getSn_SR(SOCK_SNMP_AGENT))
	{
	case SOCK_UDP:
		if ((len = getSn_RX_RSR(SOCK_SNMP_AGENT)) > 0)
		{
#if 1
			// 20231019 taylor//teddy 240122
#if ((_WIZCHIP_ == 6100) || (_WIZCHIP_ == 6300))
			request_msg.len = recvfrom(SOCK_SNMP_AGENT, request_msg.buffer, len, svr_addr, &svr_port, &addr_len);
#else
			request_msg.len = recvfrom(SOCK_SNMP_AGENT, request_msg.buffer, len, svr_addr, &svr_port);
#endif
#else
			request_msg.len = recvfrom(SOCK_SNMP_AGENT, request_msg.buffer, len, svr_addr, &svr_port);
#endif
		}
		else
		{
			request_msg.len = 0;
		}

		if (request_msg.len > 0)
		{
#ifdef _SNMP_DEBUG_
			dumpCode((void *)"\r\n[Request]\r\n", (void *)"\r\n", request_msg.buffer, request_msg.len);
#endif
			// Initialize
			request_msg.index = 0;
			response_msg.index = 0;
			errorStatus = errorIndex = 0;
			memset(response_msg.buffer, 0x00, MAX_SNMPMSG_LEN);

			// Received message parsing and send response process
			if (parseSNMPMessage() != -1)
			{
#if 1
				// 20231016 taylor//teddy 240122
#if ((_WIZCHIP_ == 6100) || (_WIZCHIP_ == 6300))
				sendto(SOCK_SNMP_AGENT, response_msg.buffer, response_msg.index, svr_addr, svr_port, 4);
#else
				sendto(SOCK_SNMP_AGENT, response_msg.buffer, response_msg.index, svr_addr, svr_port);
#endif
#else
				sendto(SOCK_SNMP_AGENT, response_msg.buffer, response_msg.index, svr_addr, svr_port);
#endif
			}

#ifdef _SNMP_DEBUG_
			dumpCode((void *)"\r\n[Response]\r\n", (void *)"\r\n", response_msg.buffer, response_msg.index);
#endif
		}
		break;

	case SOCK_CLOSED:
		if ((ret = socket(SOCK_SNMP_AGENT, Sn_MR_UDP, PORT_SNMP_AGENT, 0x00)) != SOCK_SNMP_AGENT)
			return ret;
#ifdef _SNMP_DEBUG_
		printf(" - [%d] UDP Socket for SNMP Agent, port [%d]\r\n", SOCK_SNMP_AGENT, PORT_SNMP_AGENT);
#endif
		break;

	default:
		break;
	}

	return 1;
}

int32_t findEntry(uint8_t *oid, int32_t len)
{
	int32_t i;

	for (i = 0; i < maxData; i++)
	{
		if (len == snmpData[i].oidlen)
		{
			if (!memcmp(snmpData[i].oid, oid, len))
				return (i);
		}
	}

	return OID_NOT_FOUND;
}

int32_t getOID(int32_t id, uint8_t *oid, uint8_t *len)
{
	int32_t j;

	if (!((id >= 0) && (id < maxData)))
		return INVALID_ENTRY_ID;

	*len = snmpData[id].oidlen;

	for (j = 0; j < *len; j++)
	{
		oid[j] = snmpData[id].oid[j];
	}

	return SNMP_SUCCESS;
}

int32_t getValue(uint8_t *vptr, int32_t vlen)
{
	int32_t index = 0;
	int32_t value = 0;

	while (index < vlen)
	{
		if (index != 0)
			value <<= 8;
		value |= vptr[index++];
	}

	return value;
}

int32_t getEntry(int32_t id, uint8_t *dataType, void *ptr, int32_t *len)
{
	uint8_t *ptr_8;
	int32_t value;

	uint8_t *string;
	int32_t j;

	if (!((id >= 0) && (id < maxData)))
		return INVALID_ENTRY_ID;

	*dataType = snmpData[id].dataType;

	switch (*dataType)
	{
	case SNMPDTYPE_OCTET_STRING:
	case SNMPDTYPE_OBJ_ID:
	{
		string = ptr;

		if (snmpData[id].getfunction != NULL)
		{
			snmpData[id].getfunction((void *)&snmpData[id].u.octetstring, &snmpData[id].dataLen);
		}

		if ((*dataType) == SNMPDTYPE_OCTET_STRING)
		{
			snmpData[id].dataLen = (uint8_t)strlen((char const *)&snmpData[id].u.octetstring);
		}

		*len = snmpData[id].dataLen;
		for (j = 0; j < *len; j++)
		{
			string[j] = snmpData[id].u.octetstring[j];
		}
	}
	break;

	case SNMPDTYPE_INTEGER:
	case SNMPDTYPE_TIME_TICKS:
	case SNMPDTYPE_COUNTER:
	case SNMPDTYPE_GAUGE:
	{
		if (snmpData[id].getfunction != NULL)
		{
			snmpData[id].getfunction((void *)&snmpData[id].u.intval, &snmpData[id].dataLen);
		}

		if (snmpData[id].dataLen)
			*len = snmpData[id].dataLen;
		else
			*len = sizeof(uint32_t);

		/*
		// Original code (IAR, STM32)
		// This code is not working in NXP+LPCXpresso (32-bit pointer operation error)
		value = (int32_t *)ptr;
		*value = HTONL(snmpData[id].u.intval);
		*/

		ptr_8 = ptr;
		// value = HTONL(snmpData[id].u.intval); // Endian convert when processing 32bit pointer operation
		value = snmpData[id].u.intval;

		for (j = 0; j < *len; j++)
		{
			ptr_8[j] = (uint8_t)((value >> ((*len - j - 1) * 8)));
		}
	}
	break;

	default:
		return INVALID_DATA_TYPE;
	}

	return SNMP_SUCCESS;
}

int32_t setEntry(int32_t id, void *val, int32_t vlen, uint8_t dataType, int32_t index)
{

	int32_t retStatus = OID_NOT_FOUND;
	int32_t j;

	if (snmpData[id].dataType != dataType)
	{
		errorStatus = BAD_VALUE;
		errorIndex = index;
		return INVALID_DATA_TYPE;
	}

	switch (snmpData[id].dataType)
	{
	case SNMPDTYPE_OCTET_STRING:
	case SNMPDTYPE_OBJ_ID:
	{
		uint8_t *string = val;
		for (j = 0; j < vlen; j++)
		{
			snmpData[id].u.octetstring[j] = string[j];
		}
		snmpData[id].dataLen = vlen;
	}
		retStatus = SNMP_SUCCESS;
		break;

	case SNMPDTYPE_INTEGER:
	case SNMPDTYPE_TIME_TICKS:
	case SNMPDTYPE_COUNTER:
	case SNMPDTYPE_GAUGE:
	{
		snmpData[id].u.intval = getValue((uint8_t *)val, vlen);
		snmpData[id].dataLen = vlen;

		if (snmpData[id].setfunction != NULL)
		{
			snmpData[id].setfunction(snmpData[id].u.intval);
		}
	}
		retStatus = SNMP_SUCCESS;
		break;

	default:
		retStatus = INVALID_DATA_TYPE;
		break;
	}

	return retStatus;
}

int32_t parseLength(const uint8_t *msg, int32_t *len)
{
	int32_t i = 1;

	if (msg[0] & 0x80)
	{
		int32_t tlen = (msg[0] & 0x7f) - 1;
		*len = msg[i++];

		while (tlen--)
		{
			*len <<= 8;
			*len |= msg[i++];
		}
	}
	else
	{
		*len = msg[0];
	}

	return i;
}

int32_t parseTLV(const uint8_t *msg, int32_t index, tlvStructType *tlv)
{
	int32_t Llen = 0;

	tlv->start = index;

	Llen = parseLength((const uint8_t *)&msg[index + 1], &tlv->len);

	tlv->vstart = index + Llen + 1;

	switch (msg[index])
	{
	case SNMPDTYPE_SEQUENCE:
	case GET_REQUEST:
	case GET_NEXT_REQUEST:
	case SET_REQUEST:
		tlv->nstart = tlv->vstart;
		break;
	default:
		tlv->nstart = tlv->vstart + tlv->len;
		break;
	}

	return 0;
}

void insertRespLen(int32_t reqStart, int32_t respStart, int32_t size)
{
	int32_t indexStart, lenLength;
	uint32_t mask = 0xff;
	int32_t shift = 0;

	if (request_msg.buffer[reqStart + 1] & 0x80)
	{
		lenLength = request_msg.buffer[reqStart + 1] & 0x7f;
		indexStart = respStart + 2;

		while (lenLength--)
		{
			response_msg.buffer[indexStart + lenLength] =
				(uint8_t)((size & mask) >> shift);
			shift += 8;
			mask <<= shift;
		}
	}
	else
	{
		response_msg.buffer[respStart + 1] = (uint8_t)(size & 0xff);
	}
}

int32_t parseVarBind(int32_t reqType, int32_t index)
{
	int32_t seglen = 0, id;
	tlvStructType name, value;
	int32_t size = 0;

	// extern const int32_t maxData;

	parseTLV(request_msg.buffer, request_msg.index, &name);

	if (request_msg.buffer[name.start] != SNMPDTYPE_OBJ_ID)
		return -1;

	id = findEntry(&request_msg.buffer[name.vstart], name.len);

	if ((reqType == GET_REQUEST) || (reqType == SET_REQUEST))
	{
		seglen = name.nstart - name.start;
		COPY_SEGMENT(name);
		size = seglen;
	}
	else if (reqType == GET_NEXT_REQUEST)
	{
		response_msg.buffer[response_msg.index] = request_msg.buffer[name.start];

		if (++id >= maxData)
		{
			id = OID_NOT_FOUND;
			seglen = name.nstart - name.start;
			COPY_SEGMENT(name);
			size = seglen;
		}
		else
		{
			request_msg.index += name.nstart - name.start;

			getOID(id, &response_msg.buffer[response_msg.index + 2], &response_msg.buffer[response_msg.index + 1]);

			seglen = response_msg.buffer[response_msg.index + 1] + 2;
			response_msg.index += seglen;
			size = seglen;
		}
	}

	parseTLV(request_msg.buffer, request_msg.index, &value);

	if (id != OID_NOT_FOUND)
	{
		uint8_t dataType;
		int32_t len;

		if ((reqType == GET_REQUEST) || (reqType == GET_NEXT_REQUEST))
		{
			getEntry(id, &dataType, &response_msg.buffer[response_msg.index + 2], &len);

			response_msg.buffer[response_msg.index] = dataType;
			response_msg.buffer[response_msg.index + 1] = len;
			seglen = (2 + len);
			response_msg.index += seglen;

			request_msg.index += (value.nstart - value.start);
		}
		else if (reqType == SET_REQUEST)
		{
			setEntry(id, &request_msg.buffer[value.vstart], value.len, request_msg.buffer[value.start], index);
			seglen = value.nstart - value.start;
			COPY_SEGMENT(value);
		}
	}
	else
	{
		seglen = value.nstart - value.start;
		COPY_SEGMENT(value);

		errorIndex = index;
		errorStatus = NO_SUCH_NAME;
	}

	size += seglen;

	return size;
}

int32_t parseSequence(int32_t reqType, int32_t index)
{
	int32_t seglen;
	tlvStructType seq;
	int32_t size = 0, respLoc;

	parseTLV(request_msg.buffer, request_msg.index, &seq);

	if (request_msg.buffer[seq.start] != SNMPDTYPE_SEQUENCE)
		return -1;

	seglen = seq.vstart - seq.start;
	respLoc = response_msg.index;
	COPY_SEGMENT(seq);

	size = parseVarBind(reqType, index);
	insertRespLen(seq.start, respLoc, size);
	size += seglen;

	return size;
}

int32_t parseSequenceOf(int32_t reqType)
{
	int32_t seglen;
	tlvStructType seqof;
	int32_t size = 0, respLoc;
	int32_t index = 0;

	parseTLV(request_msg.buffer, request_msg.index, &seqof);

	if (request_msg.buffer[seqof.start] != SNMPDTYPE_SEQUENCE_OF)
		return -1;

	seglen = seqof.vstart - seqof.start;
	respLoc = response_msg.index;
	COPY_SEGMENT(seqof);

	while (request_msg.index < request_msg.len)
	{
		size += parseSequence(reqType, index++);
	}

	insertRespLen(seqof.start, respLoc, size);

	return size;
}

int32_t parseRequest()
{
	int32_t ret, seglen;
	tlvStructType snmpreq, requestid, errStatus, errIndex;
	int32_t size = 0, respLoc, reqType;

	parseTLV(request_msg.buffer, request_msg.index, &snmpreq);

	reqType = request_msg.buffer[snmpreq.start];

	if (!VALID_REQUEST(reqType))
		return -1;

	seglen = snmpreq.vstart - snmpreq.start;
	respLoc = snmpreq.start;
	size += seglen;
	COPY_SEGMENT(snmpreq);

	response_msg.buffer[snmpreq.start] = GET_RESPONSE;

	parseTLV(request_msg.buffer, request_msg.index, &requestid);
	seglen = requestid.nstart - requestid.start;
	size += seglen;
	COPY_SEGMENT(requestid);

	parseTLV(request_msg.buffer, request_msg.index, &errStatus);
	seglen = errStatus.nstart - errStatus.start;
	size += seglen;
	COPY_SEGMENT(errStatus);

	parseTLV(request_msg.buffer, request_msg.index, &errIndex);
	seglen = errIndex.nstart - errIndex.start;
	size += seglen;
	COPY_SEGMENT(errIndex);

	ret = parseSequenceOf(reqType);
	if (ret == -1)
		return -1;
	else
		size += ret;

	insertRespLen(snmpreq.start, respLoc, size);

	if (errorStatus)
	{
		response_msg.buffer[errStatus.vstart] = errorStatus;
		response_msg.buffer[errIndex.vstart] = errorIndex + 1;
	}

	return size;
}

int32_t parseCommunity()
{
	int32_t seglen;
	tlvStructType community;
	int32_t size = 0;

	parseTLV(request_msg.buffer, request_msg.index, &community);

	if (!((request_msg.buffer[community.start] == SNMPDTYPE_OCTET_STRING) && (community.len == COMMUNITY_SIZE)))
	{
		return -1;
	}

	if (!memcmp(&request_msg.buffer[community.vstart], (int8_t *)COMMUNITY, COMMUNITY_SIZE))
	{
		seglen = community.nstart - community.start;
		size += seglen;
		COPY_SEGMENT(community);

		size += parseRequest();
	}
	else
	{
		return -1;
	}

	return size;
}

int32_t parseVersion()
{
	int32_t size = 0, seglen;
	tlvStructType tlv;
	int32_t resp_version_index;

	size = parseTLV(request_msg.buffer, request_msg.index, &tlv);

	if (request_msg.buffer[tlv.start] != SNMPDTYPE_INTEGER)
		return -1;

	resp_version_index = response_msg.index + 2;
	// if (request_msg.buffer[tlv.vstart] != SNMP_V1) return -1;
	if (request_msg.buffer[tlv.vstart] != SNMP_V1 &&
		request_msg.buffer[tlv.vstart] != SNMP_V2C)
	{
		response_msg.buffer[resp_version_index] = request_msg.buffer[tlv.vstart];

		return -1;
	}
	// Khi build response, ghi lại đúng version vừa nhận (0 hoặc 1)
	seglen = tlv.nstart - tlv.start;
	size += seglen;
	COPY_SEGMENT(tlv);
	size = parseCommunity();

	if (size == -1)
		return size;
	else
		return (size + seglen);
}

int32_t parseSNMPMessage()
{
	int32_t size = 0, seglen, respLoc;
	tlvStructType tlv;

	parseTLV(request_msg.buffer, request_msg.index, &tlv);

	if (request_msg.buffer[tlv.start] != SNMPDTYPE_SEQUENCE_OF)
		return -1;

	seglen = tlv.vstart - tlv.start;
	respLoc = tlv.start;
	COPY_SEGMENT(tlv);

	size = parseVersion();

	if (size == -1)
		return -1;
	else
		size += seglen;

	insertRespLen(tlv.start, respLoc, size);

	return 0;
}

void ipToByteArray(int8_t *ip, uint8_t *pDes)
{
	uint32_t i, ip1 = 0, ip2 = 0, ip3 = 0, ip4 = 0;
	int8_t buff[32];
	uint32_t len = (uint32_t)strlen((char const *)ip);
	strcpy((char *)buff, (char const *)ip);

	for (i = 0; i < len; i++)
	{
		if (buff[i] == '.')
			buff[i] = ' ';
	}

	sscanf((char const *)buff, "%u %u %u %u", &ip1, &ip2, &ip3, &ip4);
	pDes[0] = ip1;
	pDes[1] = ip2;
	pDes[2] = ip3;
	pDes[3] = ip4;
}

#include <string.h>
#include <stdint.h>
#include "snmp.h"

extern int asn1_encode_length(uint8_t *buf, uint32_t len); // dùng lại từ snmp_sendTrapV2_fixed.c

int32_t makeTrapVariableBindings(dataEntryType *vb, void *ptr, uint32_t *len)
{
    uint8_t *p = (uint8_t *)ptr;
    int idx = 0;
    int len_bytes;

    // SEQUENCE mở đầu cho VarBind
    p[idx++] = 0x30;
    int seq_len_pos = idx; 
    idx += 3; // dành chỗ cho length (BER long form)

    // --------------- OID ---------------
    p[idx++] = 0x06;                        // OBJECT IDENTIFIER
    p[idx++] = vb->oidlen;                  // độ dài OID
    memcpy(&p[idx], vb->oid, vb->oidlen);   // copy dữ liệu OID
    idx += vb->oidlen;

    // --------------- VALUE ---------------
    switch (vb->dataType)
    {
    case SNMPDTYPE_INTEGER:
    case SNMPDTYPE_COUNTER:
    case SNMPDTYPE_GAUGE:
    {
        uint32_t val = HTONL(vb->u.intval);
        p[idx++] = 0x02;     // INTEGER
        p[idx++] = 0x04;     // length = 4
        memcpy(&p[idx], &val, 4);
        idx += 4;
        break;
    }

    case SNMPDTYPE_TIME_TICKS:
    {
        uint32_t val = HTONL(vb->u.intval);
        p[idx++] = 0x43;     // TimeTicks
        p[idx++] = 0x04;
        memcpy(&p[idx], &val, 4);
        idx += 4;
        break;
    }

    case SNMPDTYPE_OCTET_STRING:
    {
        uint8_t str_len = vb->dataLen;
        p[idx++] = 0x04;     // OCTET STRING
        p[idx++] = str_len;
        memcpy(&p[idx], vb->u.octetstring, str_len);
        idx += str_len;
        break;
    }

    case SNMPDTYPE_OBJ_ID:
    {
        uint8_t len_oid = vb->dataLen;
        p[idx++] = 0x06; // OID type
        p[idx++] = len_oid;
        memcpy(&p[idx], vb->u.octetstring, len_oid);
        idx += len_oid;
        break;
    }

    default:
        return INVALID_DATA_TYPE;
    }

    // --------------- Cập nhật SEQUENCE length ---------------
    uint32_t seq_len = idx - seq_len_pos - 3;
    len_bytes = asn1_encode_length(&p[seq_len_pos], seq_len);
    memmove(&p[seq_len_pos + len_bytes], &p[seq_len_pos + 3], idx - (seq_len_pos + 3));
    idx -= (3 - len_bytes);

    *len = idx;
    return SNMP_SUCCESS;
}

int32_t snmp_sendTrap(uint8_t *managerIP, uint8_t *agentIP, int8_t *community, dataEntryType enterprise_oid, uint32_t genericTrap, uint32_t specificTrap, uint32_t va_count, ...)
{
	uint32_t i;
	int32_t packet_index = 0;
	int32_t packet_buff1 = 0;
	int32_t packet_buff2 = 0;
	int32_t packet_buff3 = 0;

	va_list ap;
	uint32_t length_var_bindings = 0;
	uint32_t length_buff = 0;

	// SNMP Trap packet generation
	packet_trap[packet_index++] = 0x30; // ASN.1 Header

	packet_trap[packet_index] = 0xff; // pdu_length, temp
	packet_buff1 = packet_index++;

	packet_trap[packet_index++] = 0x02; // Version
	packet_trap[packet_index++] = 0x01;
	packet_trap[packet_index++] = 0x00;

	packet_trap[packet_index++] = 0x04; // Community
	packet_trap[packet_index++] = (uint8_t)strlen((char const *)community);
	memcpy(&(packet_trap[packet_index]), community, strlen((char const *)community));

	packet_index = packet_index + (uint8_t)strlen((char const *)community);

	packet_trap[packet_index++] = 0xa4; // trap
	packet_trap[packet_index] = 0xff;	// length, temp
	packet_buff2 = packet_index++;

	packet_trap[packet_index++] = 0x06; // enterprise_oid
	packet_trap[packet_index++] = enterprise_oid.oidlen;
	for (i = 0; i < enterprise_oid.oidlen; i++)
	{
		packet_trap[packet_index++] = enterprise_oid.oid[i];
	}

	packet_trap[packet_index++] = 0x40; // agent ip
	packet_trap[packet_index++] = 0x04;
	packet_trap[packet_index++] = agentIP[0];
	packet_trap[packet_index++] = agentIP[1];
	packet_trap[packet_index++] = agentIP[2];
	packet_trap[packet_index++] = agentIP[3];

	packet_trap[packet_index++] = 0x02; // Generic Trap
	packet_trap[packet_index++] = 0x01;
	packet_trap[packet_index++] = (uint8_t)genericTrap;

	packet_trap[packet_index++] = 0x02; // Specific Trap
	packet_trap[packet_index++] = 0x01;
	packet_trap[packet_index++] = (uint8_t)specificTrap;

	packet_trap[packet_index++] = 0x43; // Timestamp
	packet_trap[packet_index++] = 0x01;
	packet_trap[packet_index++] = 0x00;

	packet_trap[packet_index++] = 0x30; // Sequence of variable-bindings
	packet_trap[packet_index] = 0xff;
	packet_buff3 = packet_index++;

	// variable-bindings
	{
		va_start(ap, va_count);

		for (i = 0; i < va_count; i++)
		{
			dataEntryType *fff = va_arg(ap, dataEntryType *);
			makeTrapVariableBindings(fff, &(packet_trap[packet_index]), &length_buff);
			packet_index = packet_index + length_buff;
			length_var_bindings = length_var_bindings + length_buff;
		}

		packet_trap[packet_buff3] = length_var_bindings;

		va_end(ap);
	}

	packet_trap[packet_buff1] = packet_index - 2;
	packet_trap[packet_buff2] = packet_index - (9 + (uint8_t)strlen((char const *)community));

	// Send SNMP Trap Packet to NMS
	{
		socket(SOCK_SNMP_TRAP, Sn_MR_UDP, PORT_SNMP_TRAP, 0);
#if 1
		// 20231016 taylor//teddy 240122
#if ((_WIZCHIP_ == 6100) || (_WIZCHIP_ == 6300))
		sendto(SOCK_SNMP_TRAP, packet_trap, packet_index, managerIP, PORT_SNMP_TRAP, 4);
#else
		sendto(SOCK_SNMP_TRAP, packet_trap, packet_index, managerIP, PORT_SNMP_TRAP);
#endif
#else
		sendto(SOCK_SNMP_TRAP, packet_trap, packet_index, managerIP, PORT_SNMP_TRAP);
#endif

		close(SOCK_SNMP_TRAP);
		return 0;
	}
}

#ifdef _SNMP_DEBUG_
void dumpCode(uint8_t *header, uint8_t *tail, uint8_t *buff, int32_t len)
{
	int i;

	printf((char const *)header);

	for (i = 0; i < len; i++)
	{
		if (i % 16 == 0)
			printf("0x%04x : ", i);
		printf("%02x ", buff[i]);

		if (i % 16 - 15 == 0)
		{
			int j;
			printf("  ");
			for (j = i - 15; j <= i; j++)
			{
				if (isprint(buff[j]))
					printf("%c", buff[j]);
				else
					printf(".");
			}
			printf("\r\n");
		}
	}

	if (i % 16 != 0)
	{
		int j;
		int spaces = (len - i + 16 - i % 16) * 3 + 2;
		for (j = 0; j < spaces; j++)
			printf(" ");
		for (j = i - i % 16; j < len; j++)
		{
			if (isprint(buff[j]))
				printf("%c", buff[j]);
			else
				printf(".");
		}
	}
	printf((char const *)tail);
}
#endif

// ==========================
// ASN.1 Length Encoder
// ==========================
static int asn1_encode_length(uint8_t *buf, uint32_t len)
{
    if (len < 128)
    {
        buf[0] = (uint8_t)len;
        return 1;
    }
    else if (len < 256)
    {
        buf[0] = 0x81;
        buf[1] = (uint8_t)len;
        return 2;
    }
    else
    {
        buf[0] = 0x82;
        buf[1] = (uint8_t)(len >> 8);
        buf[2] = (uint8_t)(len);
        return 3;
    }
}

// ==========================
// SNMPv2 Trap Sender
// ==========================
int32_t snmp_sendTrapV2(uint8_t *managerIP, int8_t *community,
                        const uint8_t *trap_oid, uint8_t trap_oid_len,
                        dataEntryType *extra_vbs, uint32_t extra_count)
{
    uint8_t *p = packet_trap;
    int idx = 0;
    int len_bytes;

    // ---------- SNMP Message Sequence ----------
    p[idx++] = 0x30; // SEQUENCE
    int len_msg_pos = idx; idx += 3; // reserve 3 bytes max for long-form

    // ---------- Version ----------
    p[idx++] = 0x02; p[idx++] = 0x01; p[idx++] = 0x01; // v2c = 1

    // ---------- Community ----------
    uint8_t comm_len = (uint8_t)strlen((char *)community);
    p[idx++] = 0x04; p[idx++] = comm_len;
    memcpy(&p[idx], community, comm_len); idx += comm_len;

    // ---------- PDU ----------
    p[idx++] = 0xA7; // SNMPv2-Trap
    int len_pdu_pos = idx; idx += 3; // reserve for length

    // request-id, error-status, error-index = 0
    uint8_t req_hdr[] = {
        0x02, 0x01, 0x00,
        0x02, 0x01, 0x00,
        0x02, 0x01, 0x00
    };
    memcpy(&p[idx], req_hdr, sizeof(req_hdr)); idx += sizeof(req_hdr);

    // ---------- VarBindList ----------
    p[idx++] = 0x30;
    int len_vbl_pos = idx; idx += 3;

    // VarBind 1: sysUpTime.0
    uint8_t sysUpTime_oid[] = {1,3,6,1,2,1,1,3,0};
    uint32_t ticks; uint8_t len_tmp;
    currentUptime(&ticks, &len_tmp);

    p[idx++] = 0x30;
    int v1_len_pos = idx; idx += 3;
    p[idx++] = 0x06; p[idx++] = sizeof(sysUpTime_oid);
    memcpy(&p[idx], sysUpTime_oid, sizeof(sysUpTime_oid)); idx += sizeof(sysUpTime_oid);
    p[idx++] = 0x43; p[idx++] = 0x04;
    p[idx++] = (uint8_t)(ticks >> 24);
    p[idx++] = (uint8_t)(ticks >> 16);
    p[idx++] = (uint8_t)(ticks >> 8);
    p[idx++] = (uint8_t)(ticks);
    len_bytes = asn1_encode_length(&p[v1_len_pos], idx - v1_len_pos - 3);
    memmove(&p[v1_len_pos + len_bytes], &p[v1_len_pos + 3], idx - (v1_len_pos + 3));
    idx -= (3 - len_bytes);

    // VarBind 2: snmpTrapOID.0
    uint8_t snmpTrapOID0[] = {1,3,6,1,6,3,1,1,4,1,0};
    p[idx++] = 0x30;
    int v2_len_pos = idx; idx += 3;
    p[idx++] = 0x06; p[idx++] = sizeof(snmpTrapOID0);
    memcpy(&p[idx], snmpTrapOID0, sizeof(snmpTrapOID0)); idx += sizeof(snmpTrapOID0);
    p[idx++] = 0x06; p[idx++] = trap_oid_len;
    memcpy(&p[idx], trap_oid, trap_oid_len); idx += trap_oid_len;
    len_bytes = asn1_encode_length(&p[v2_len_pos], idx - v2_len_pos - 3);
    memmove(&p[v2_len_pos + len_bytes], &p[v2_len_pos + 3], idx - (v2_len_pos + 3));
    idx -= (3 - len_bytes);

    // VarBind 3+: extra VBs
    for (uint32_t i = 0; i < extra_count; i++)
    {
        uint32_t vb_len = 0;
        makeTrapVariableBindings(&extra_vbs[i], &p[idx], &vb_len);
        idx += vb_len;
    }

    // ---------- Fill lengths ----------
    uint32_t vbl_len = idx - (len_vbl_pos + 3);
    len_bytes = asn1_encode_length(&p[len_vbl_pos], vbl_len);
    memmove(&p[len_vbl_pos + len_bytes], &p[len_vbl_pos + 3], idx - (len_vbl_pos + 3));
    idx -= (3 - len_bytes);

    uint32_t pdu_len = idx - (len_pdu_pos + 3);
    len_bytes = asn1_encode_length(&p[len_pdu_pos], pdu_len);
    memmove(&p[len_pdu_pos + len_bytes], &p[len_pdu_pos + 3], idx - (len_pdu_pos + 3));
    idx -= (3 - len_bytes);

    uint32_t msg_len = idx - (len_msg_pos + 3);
    len_bytes = asn1_encode_length(&p[len_msg_pos], msg_len);
    memmove(&p[len_msg_pos + len_bytes], &p[len_msg_pos + 3], idx - (len_msg_pos + 3));
    idx -= (3 - len_bytes);

    // ---------- Debug output ----------
    printf("[SNMP] Sending Trap (%d bytes): ", idx);
    for (int i = 0; i < idx; i++) printf("%02X ", p[i]);
    printf("\n");

    // ---------- Send Trap ----------
    socket(SOCK_SNMP_TRAP, Sn_MR_UDP, PORT_SNMP_TRAP, 0);
    sendto(SOCK_SNMP_TRAP, p, idx, managerIP, PORT_SNMP_TRAP);
    close(SOCK_SNMP_TRAP);

    return SNMP_SUCCESS;
}