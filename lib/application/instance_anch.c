/*
 * 文件名：instance_anch.c
 * 修改日期：2019年9月21日
 * 基站TWR程序
 *
 */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#include "compiler.h"
#include "port.h"
#include "deca_device_api.h"
#include "deca_spi.h"
#include "deca_regs.h"
#include "instance.h"

// ----------------------------------------------------------------------------------------------
// 函数声明
// ----------------------------------------------------------------------------------------------
void anch_prepare_anc2tag_response(unsigned int tof_index, uint8 srcAddr_index, uint8 fcode_index, uint8 *frame, uint32 uTimeStamp);
void anch_enable_rx(uint32 dlyTime);

/*
 * 函数名称：void anch_no_timeout_rx_reenable(void)
 * 主要功能：
 * 打开接收机，不设置超时，用于第一次接收poll数据
 */
void anch_no_timeout_rx_reenable(void)
{
	dwt_setrxtimeout(0); // reconfigure the timeout
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

// DISCOVERY 模式
#if (DISCOVERY == 1)

int anch_add_tag_to_list(uint8 *tagAddr)
{
	instance_data_t *inst = instance_get_local_structure_ptr();
	uint8 i;
	uint8 blank[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	int slot = -1;

	// add the new Tag to the list, if not already there and there is space
	for (i = 0; i < MAX_TAG_LIST_SIZE; i++)
	{
		if (memcmp(&inst->tagList[i][0], &tagAddr[0], 8) != 0)
		{
			if (memcmp(&inst->tagList[i][0], &blank[0], 8) == 0) // blank entry
			{
				memcpy(&inst->tagList[i][0], &tagAddr[0], 8);
				inst->tagListLen = i + 1;
				slot = i;
				break;
			}
		}
		else
		{
			slot = i;
			break; // we already have this Tag in the list
		}
	}

	return slot;
}

int anch_prepare_anc2tag_rangeinitresponse(uint8 *tagID, uint8 slot, uint32 uTimeStamp)
{
	int typePend = DWT_SIG_DW_IDLE;
	uint16 frameLength = 0;
	instance_data_t *inst = instance_get_local_structure_ptr();

	inst->psduLength = frameLength = RANGINGINIT_MSG_LEN + FRAME_CRTL_AND_ADDRESS_LS + FRAME_CRC;

	memcpy(&inst->rng_initmsg.destAddr[0], tagID, ADDR_BYTE_SIZE_L); // remember who to send the reply to (set destination address)
	inst->rng_initmsg.sourceAddr[0] = inst->eui64[0];
	inst->rng_initmsg.sourceAddr[1] = inst->eui64[1];

	inst->rng_initmsg.seqNum = inst->frameSN++;

	{
		int error = 0;
		int currentSlotTime = 0;
		int expectedSlotTime = 0;

		currentSlotTime = uTimeStamp % inst->sframePeriod_ms;
		expectedSlotTime = (slot)*inst->slotDuration_ms; //
		error = expectedSlotTime - currentSlotTime;

		if (error < (-(inst->sframePeriod_ms >> 1)))
		{
			inst->tagSleepCorrection_ms = (inst->sframePeriod_ms + error);
		}
		else
		{
			inst->tagSleepCorrection_ms = error;
		}

		inst->tagSleepCorrection_ms += inst->sframePeriod_ms;
		inst->rng_initmsg.messageData[RES_TAG_SLP0] = inst->tagSleepCorrection_ms & 0xFF;
		inst->rng_initmsg.messageData[RES_TAG_SLP1] = (inst->tagSleepCorrection_ms >> 8) & 0xFF;
	}

	inst->rng_initmsg.messageData[RES_TAG_ADD0] = slot & 0xFF;
	inst->rng_initmsg.messageData[RES_TAG_ADD1] = (slot >> 8) & 0xFF;
	inst->rng_initmsg.messageData[FCODE] = RTLS_DEMO_MSG_RNG_INIT;
	dwt_writetxfctrl(frameLength, 0, 1);
	dwt_writetxdata(frameLength, (uint8 *)&inst->rng_initmsg, 0);

	dwt_setrxaftertxdelay(0);

	inst->wait4ack = DWT_RESPONSE_EXPECTED; // re has/will be re-enabled
	dwt_setdelayedtrxtime(inst->delayedTRXTime32h);
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED))
	{

		dwt_setrxaftertxdelay(0);
		inst->wait4ack = 0;
	}
	else
	{
		typePend = DWT_SIG_TX_PENDING;
		inst->timeofTx = portGetTickCnt();
		inst->monitor = 1;
	}

	return typePend;
}
#endif

/*
 * 函数名称：void anch_enable_rx(uint32 dlyTime)
 * 主要功能：
 * 接收消息超时事件
 */
void anch_process_RX_timeout(instance_data_t *inst)
{
	if (inst->mode == ANCHOR)
	{
		inst->AppState = TA_RXE_WAIT; // 重新打开接收机进入新的测距周期
		dwt_setrxtimeout(0);
		inst->wait4ack = 0;
	}
}

/*
 * 函数名称：void anch_enable_rx(uint32 dlyTime)
 * 主要功能：
 * 打开接收机
 */
void anch_enable_rx(uint32 dlyTime)
{
	instance_data_t *inst = instance_get_local_structure_ptr();
	dwt_setdelayedtrxtime(dlyTime - inst->preambleDuration32h);
	if (dwt_rxenable(DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR)) // delayed rx
	{
		dwt_setrxtimeout((uint16)inst->fwto4RespFrame_sy * 2);
		dwt_rxenable(DWT_START_RX_IMMEDIATE);
		dwt_setrxtimeout((uint16)inst->fwto4RespFrame_sy);
	}
}

/*
 * 函数名称：uint8 anch_txresponse_or_rx_reenable(void)
 * 主要功能：
 * 打开接收机接收数据和发送resp消息
 */
uint8 anch_txresponse_or_rx_reenable(void)
{
	uint8 typePend = DWT_SIG_DW_IDLE;
	int sendResp = 0;
	instance_data_t *inst = instance_get_local_structure_ptr();

	if (inst->remainingRespToRx == 0) // go back to RX without timeout - ranging has finished. (wait for Final but no timeout)
	{
		dwt_setrxtimeout(inst->fwto4FinalFrame_sy * 2); // reconfigure the timeout for the final
		inst->wait4final = WAIT4TAGFINAL;
	}

	if ((inst->remainingRespToRx + inst->shortAdd_idx) == NUM_EXPECTED_RESPONSES)
	{
		sendResp = 1;
	}

	inst->delayedTRXTime32h += inst->fixedReplyDelayAnc32h;

	if (sendResp == 1)
	{
		inst->wait4ack = DWT_RESPONSE_EXPECTED; // re has/will be re-enabled

		dwt_setdelayedtrxtime(inst->delayedTRXTime32h);
		if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED))
		{
			// if TX has failed - we need to re-enable RX for the next response or final reception...
			dwt_setrxaftertxdelay(0);
			inst->wait4ack = 0;				  // clear the flag as the TX has failed the TRX is off
			if (inst->remainingRespToRx == 0) // not expecting any more responses - enable RX
			{
				inst->delayedTRXTime32h = (inst->tagPollRxTime >> 8) + inst->pollTx2FinalTxDelay;
			}
			else
			{
				inst->delayedTRXTime32h += 2 * (inst->fixedReplyDelayAnc32h); // to take into account W4R
			}
			anch_enable_rx(inst->delayedTRXTime32h);
			typePend = DWT_SIG_RX_PENDING;
		}
		else
		{
			inst->delayedTRXTime32h += inst->fixedReplyDelayAnc32h; // to take into account W4R
			typePend = DWT_SIG_TX_PENDING;							// exit this interrupt and notify the application/instance that TX is in progress.
			inst->timeofTx = portGetTickCnt();
			inst->monitor = 1;
		}
	}
	else // stay in receive
	{
		if (inst->remainingRespToRx == 0) // not expecting any more responses - enable RX
		{
			inst->delayedTRXTime32h = (inst->tagPollRxTime >> 8) + (inst->pollTx2FinalTxDelay >> 8);
			dwt_setdelayedtrxtime(inst->delayedTRXTime32h - inst->preambleDuration32h);
			if (dwt_rxenable(DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR)) // delayed rx
			{
				anch_no_timeout_rx_reenable();
			}
		}
		else
		{
			anch_enable_rx(inst->delayedTRXTime32h);
		}

		typePend = DWT_SIG_RX_PENDING;
	}
	return typePend;
}

/*
 * 函数名称：void anch_prepare_anc2tag_response(unsigned int tof_idx, uint8 srcAddr_index, uint8 fcode_index, uint8 *frame, uint32 uTimeStamp)
 * 主要功能：
 * 打包resp消息数据
 */
void anch_prepare_anc2tag_response(unsigned int tof_idx, uint8 srcAddr_index, uint8 fcode_index, uint8 *frame, uint32 uTimeStamp)
{
	uint16 frameLength = 0;
	instance_data_t *inst = instance_get_local_structure_ptr();
	int tagSleepCorrection_ms = 0;

	inst->psduLength = frameLength = ANCH_RESPONSE_MSG_LEN + FRAME_CRTL_AND_ADDRESS_S + FRAME_CRC;
	memcpy(&inst->msg_f.destAddr[0], &frame[srcAddr_index], ADDR_BYTE_SIZE_S); // remember who to send the reply to (set destination address)
	inst->msg_f.sourceAddr[0] = inst->eui64[0];
	inst->msg_f.sourceAddr[1] = inst->eui64[1];
	// Write calculated TOF into response message (get the previous ToF+range number from that tag)
	memcpy(&(inst->msg_f.messageData[TOFR]), &inst->tof[tof_idx], 4);
	inst->msg_f.messageData[TOFRN] = inst->rangeNumA[tof_idx]; // get the previous range number

	inst->rangeNumA[tof_idx] = 0;
	inst->rangeNum = frame[POLL_RNUM + fcode_index];
	inst->msg_f.seqNum = inst->frameSN++;

	// we have our range - update the own mask entry...
	if (inst->tof[tof_idx] != INVALID_TOF) // check the last ToF entry is valid and copy into the current array
	{
		inst->rxResponseMask = (0x1 << inst->shortAdd_idx);
		inst->tofArray[inst->shortAdd_idx] = inst->tof[tof_idx];
	}
	else
	{
		inst->tofArray[inst->shortAdd_idx] = INVALID_TOF;
		inst->rxResponseMask = 0; // reset the mask of received responses when rx poll
	}

	dwt_setrxaftertxdelay(inst->ancRespRxDelay_sy);

	if (inst->gatewayAnchor)
	{
		int error = 0;
		int currentSlotTime = 0;
		int expectedSlotTime = 0;
		currentSlotTime = uTimeStamp % inst->sframePeriod_ms;
		expectedSlotTime = tof_idx * inst->slotDuration_ms;
		error = expectedSlotTime - currentSlotTime;

		if (error < (-(inst->sframePeriod_ms >> 1))) // if error is more negative than 0.5 period, add whole period to give up to 1.5 period sleep
		{
			tagSleepCorrection_ms = (inst->sframePeriod_ms + error);
		}
		else // the minimum Sleep time will be 0.5 period
		{
			tagSleepCorrection_ms = error;
		}
		inst->msg_f.messageData[RES_TAG_SLP0] = tagSleepCorrection_ms & 0xFF;
		inst->msg_f.messageData[RES_TAG_SLP1] = (tagSleepCorrection_ms >> 8) & 0xFF;
	}
	else
	{
		tagSleepCorrection_ms = 0;
		inst->msg_f.messageData[RES_TAG_SLP0] = 0;
		inst->msg_f.messageData[RES_TAG_SLP1] = 0;
	}
	inst->msg_f.messageData[FCODE] = RTLS_DEMO_MSG_ANCH_RESP; // message function code (specifies if message is a poll, response or other...)

	dwt_writetxfctrl(frameLength, 0, 1);
	dwt_writetxdata(frameLength, (uint8 *)&inst->msg_f, 0); // write the frame data
}

/*
 * 函数名称：void anch_handle_error_unknownframe_timeout(event_data_t dw_event)
 * 主要功能：
 * 接收未知消息回调函数
 */
void anch_handle_error_unknownframe_timeout(event_data_t dw_event)
{
	instance_data_t *inst = instance_get_local_structure_ptr();
	dw_event.type = 0;
	dw_event.rxLength = 0;
	if (inst->remainingRespToRx >= 0)
	{
		inst->remainingRespToRx--;
	}

	switch (inst->mode)
	{
	case ANCHOR:
	{
		if (inst->twrMode == RESPONDER_T)
		{
			if (inst->wait4final == WAIT4TAGFINAL)
			{
				inst->twrMode = LISTENER;
				inst->wait4final = 0;

				dw_event.typePend = DWT_SIG_DW_IDLE;
				instance_putevent(dw_event, DWT_SIG_RX_TIMEOUT);
			}
			else
			{
				dw_event.typePend = anch_txresponse_or_rx_reenable();
				if (dw_event.typePend != DWT_SIG_RX_PENDING)
				{
					instance_putevent(dw_event, DWT_SIG_RX_TIMEOUT);
				}
			}
			break;
		}
	}
	default:
	{
		dwt_rxenable(DWT_START_RX_IMMEDIATE);
	}
	break;
	}
}

/*
 * 函数名称：void rx_to_cb_anch(const dwt_cb_data_t *rxd)
 * 主要功能：
 * 接收消息超时回调函数
 */
void rx_to_cb_anch(const dwt_cb_data_t *rxd)
{
	event_data_t dw_event;
	dw_event.uTimeStamp = portGetTickCnt();
	anch_handle_error_unknownframe_timeout(dw_event);
}

/*
 * 函数名称：void rx_err_cb_anch(const dwt_cb_data_t *rxd)
 * 主要功能：
 * 接收消息错误回调函数
 */
void rx_err_cb_anch(const dwt_cb_data_t *rxd)
{
	event_data_t dw_event;
	dw_event.uTimeStamp = portGetTickCnt();
	anch_handle_error_unknownframe_timeout(dw_event);
}

/*
 * 函数名称：void rx_ok_cb_anch(const dwt_cb_data_t *rxd)
 * 主要功能：
 * 接收消息正确回调函数
 */
void rx_ok_cb_anch(const dwt_cb_data_t *rxd)
{
	instance_data_t *inst = instance_get_local_structure_ptr();
	uint8 rxTimeStamp[5] = {0, 0, 0, 0, 0};

	uint8 rxd_event = 0;
	uint8 fcode_index = 0;
	uint8 srcAddr_index = 0;
	event_data_t dw_event;

	dw_event.uTimeStamp = portGetTickCnt();
	dw_event.rxLength = rxd->datalength;

	if ((rxd->fctrl[0] == 0x41) && ((rxd->fctrl[1] & 0xCC) == 0x88)) // 短地址消息
	{
		fcode_index = FRAME_CRTL_AND_ADDRESS_S;
		srcAddr_index = FRAME_CTRLP + ADDR_BYTE_SIZE_S;
		rxd_event = DWT_SIG_RX_OKAY;
	}
#if (DISCOVERY == 1)
	else if (inst->gatewayAnchor && (rxd->fctrl[0] == 0xC5) && (inst->twrMode == LISTENER)) // only gateway anchor processes blinks
	{
		rxd_event = DWT_SIG_RX_BLINK;
	}
#endif
	else
	{
		rxd_event = SIG_RX_UNKNOWN;
	}

	// 读取时间戳
	dwt_readrxtimestamp(rxTimeStamp);
	dwt_readrxdata((uint8 *)&dw_event.msgu.frame[0], rxd->datalength, 0); // Read Data Frame
	instance_seteventtime(&dw_event, rxTimeStamp);
	dw_event.type = 0;
	dw_event.typePend = DWT_SIG_DW_IDLE;
#if (DISCOVERY == 1) // DISCOVERY模式
	if (rxd_event == DWT_SIG_RX_BLINK)
	{
		int slot = anch_add_tag_to_list(&(dw_event.msgu.rxblinkmsg.tagID[0]));
		// add this Tag to the list of Tags we know about
		// if the returned value is not -1 then we want to add this tag (the returned value is the slot number)
		if (slot != -1)
		{
			inst->twrMode = RESPONDER_B;
			inst->delayedTRXTime32h = dw_event.timeStamp32h + inst->fixedReplyDelayAnc32h;
			// send response (ranging init) with slot number / tag short address
			// prepare the response and write it to the tx buffer
			dw_event.typePend = anch_prepare_anc2tag_rangeinitresponse(&(dw_event.msgu.rxblinkmsg.tagID[0]), slot, dw_event.uTimeStamp);

#include "usart.h"
			HAL_UART_Transmit_DMA(&huart2, &slot, 1);

			if (dw_event.typePend == DWT_SIG_TX_PENDING)
			{
				instance_putevent(dw_event, rxd_event);
			}
			else
			{
				anch_handle_error_unknownframe_timeout(dw_event);
			}
		}
		else
		{
			// we cannot add this tag into our system...
			anch_handle_error_unknownframe_timeout(dw_event);
		}
	}
	else
#endif
		if (rxd_event == DWT_SIG_RX_OKAY) // 接收正确消息数据
	{
		uint16 sourceAddress = (((uint16)dw_event.msgu.frame[srcAddr_index + 1]) << 8) + dw_event.msgu.frame[srcAddr_index];
		if ((dw_event.msgu.rxmsg_ss.panID[0] != (inst->panID & 0xff)) || (dw_event.msgu.rxmsg_ss.panID[1] != (inst->panID >> 8)))
		{
			anch_handle_error_unknownframe_timeout(dw_event);
			return;
		}
		// 校验消息类型是否为TWR数据
		switch (dw_event.msgu.frame[fcode_index])
		{
		case RTLS_DEMO_MSG_TAG_POLL:
		{
			led_on(LED_PC9);
			if ((inst->mode == ANCHOR) && (sourceAddress < max_tag_num))
			{
				inst->twrMode = RESPONDER_T;
				inst->wait4final = 0;
				// 准备打包RESP消息数据
				anch_prepare_anc2tag_response(sourceAddress, srcAddr_index, fcode_index, &dw_event.msgu.frame[0], dw_event.uTimeStamp);
				dwt_setrxtimeout((uint16)inst->fwto4RespFrame_sy); // 设置接收超时
				inst->delayedTRXTime32h = dw_event.timeStamp32h;
				inst->remainingRespToRx = NUM_EXPECTED_RESPONSES;
				dw_event.typePend = anch_txresponse_or_rx_reenable();
				inst->tof[sourceAddress] = INVALID_TOF;
			}
			else
			{
				anch_handle_error_unknownframe_timeout(dw_event);
				return;
			}
		}
		break;

		case RTLS_DEMO_MSG_ANCH_RESP: // 接收到基站的RESP消息
		{
			if (inst->twrMode == RESPONDER_T)
			{
				inst->rxResps++;
				inst->remainingRespToRx--;
				dw_event.typePend = anch_txresponse_or_rx_reenable();
			}
			else
			{
				anch_no_timeout_rx_reenable();
				return;
			}
		}
		break;

		case RTLS_DEMO_MSG_TAG_FINAL: // 接收到FINAL消息
		{
			led_off(LED_PC9);
			if ((inst->twrMode == RESPONDER_T) && (inst->mode == ANCHOR))
			{
				anch_no_timeout_rx_reenable();
				dw_event.typePend = DWT_SIG_RX_PENDING;
				inst->twrMode = LISTENER;
				inst->wait4final = 0;
				float rx_power = instance_getReceivePower();
				break;
			}
		}
		default:
		{
			anch_no_timeout_rx_reenable();
			return;
		}
		break;
		}
		instance_putevent(dw_event, rxd_event);
	}
	else
	{
		anch_handle_error_unknownframe_timeout(dw_event);
	}
}

/*
 * 函数名称：int check_state_change(uint8 event)
 * 主要功能：
 * 检查当前状态机状态并判断是否切换状态
 */
int check_state_change(uint8 event)
{
	int state = TA_RXE_WAIT;
	// the response has been sent - await TX done event
	if (event == DWT_SIG_TX_PENDING)
	{
		state = TA_TX_WAIT_CONF; // wait confirmation
	}
	// already re-enabled the receiver
	else if (event == DWT_SIG_RX_PENDING)
	{
		// stay in RX wait for next frame...
		// RX is already enabled...
		state = TA_RX_WAIT_DATA; // wait for next frame
	}
	else // the DW1000 is idle (re-enable from the application level)
	{
		// stay in RX change state and re-enable RX for next frame...
		state = TA_RXE_WAIT;
	}
	return state;
}

/*
 * 函数名称：int32 calc_tof(uint8 *messageData, uint64 anchorRespTxTime, uint64 tagFinalRxTime, uint64 tagPollRxTime, uint8 shortAdd_idx)
 * 主要功能：
 * 根据时间戳计算TOF
 */
int32 calc_tof(uint8 *messageData, uint64 anchorRespTxTime, uint64 tagFinalRxTime, uint64 tagPollRxTime, uint8 shortAdd_idx)
{
	int64 Rb, Da, Ra, Db;
	uint64 tagFinalTxTime = 0;
	uint64 tagPollTxTime = 0;
	uint64 anchorRespRxTime = 0;

	double RaRbxDaDb = 0;
	double RbyDb = 0;
	double RayDa = 0;
	int32 tof;
	uint8 index = RRXT0 + 5 * (shortAdd_idx);

	memcpy(&tagPollTxTime, &(messageData[PTXT]), 5);
	memcpy(&anchorRespRxTime, &(messageData[index]), 5);
	memcpy(&tagFinalTxTime, &(messageData[FTXT]), 5);

	Ra = (int64)((anchorRespRxTime - tagPollTxTime) & MASK_40BIT); // Tround1
	Db = (int64)((anchorRespTxTime - tagPollRxTime) & MASK_40BIT); // reply1

	Rb = (int64)((tagFinalRxTime - anchorRespTxTime) & MASK_40BIT); // Tround2
	Da = (int64)((tagFinalTxTime - anchorRespRxTime) & MASK_40BIT); // reply2

	RaRbxDaDb = (((double)Ra)) * (((double)Rb)) - (((double)Da)) * (((double)Db));
	RbyDb = ((double)Rb + (double)Db);
	RayDa = ((double)Ra + (double)Da);
	tof = (int32)(RaRbxDaDb / (RbyDb + RayDa));

	return tof;
}

/*
 * 函数名称：int anch_app_run(instance_data_t *inst)
 * 主要功能：
 * 基站功能状态机入口
 */
int anch_app_run(instance_data_t *inst)
{
	int instDone = INST_NOT_DONE_YET;
	int message = instance_peekevent(); // 读取事件消息

	switch (inst->AppState)
	{
	case TA_INIT: // 初始化相关
		switch (inst->mode)
		{
		case ANCHOR:
		{
			memcpy(inst->eui64, &inst->instanceAddress16, ADDR_BYTE_SIZE_S); // 写入16bit地址到EUI后2个字节
			dwt_seteui(inst->eui64);										 // 设置设备EUI
			dwt_setpanid(inst->panID);										 // 设置设备PANID

			inst->shortAdd_idx = (inst->instanceAddress16);
			dwt_setaddress16(inst->instanceAddress16); // 设置2byte短地址

			// 判断当前是否为设置网关基站A0
			if (inst->instanceAddress16 == GATEWAY_ANCHOR_ADDR)
			{
				inst->gatewayAnchor = TRUE;
			}
			dwt_enableframefilter(DWT_FF_NOTYPE_EN); // 设置帧过滤模式，允许接收帧数据和ACK数据
			dwt_setrxaftertxdelay(0);				 // 初始化发送消息后立即打开接收机
			inst->AppState = TA_RXE_WAIT;			 // 下一个状态机标志位TA_RXE_WAIT
			dwt_setrxtimeout(0);					 // 初始化接收超时为0
			instance_config_frameheader_16bit(inst); // 数据帧控制字节0X41 0X88

			// 设置DISCOVERY模式下的帧控制字节为0X41 0X8C
			inst->rng_initmsg.frameCtrl[0] = 0x1 | 0x40;
			inst->rng_initmsg.frameCtrl[1] = 0xC | 0x80;

			// 设置DISCOVERY模式下的PANID
			inst->rng_initmsg.panID[0] = (inst->panID) & 0xff;
			inst->rng_initmsg.panID[1] = inst->panID >> 8;
		}
		break;
		default:
			break;
		}
		break; // end case TA_INIT

	case TA_TX_WAIT_CONF:
	{
		static uint32 count = 0;
		event_data_t *dw_event = instance_getevent(); // 读取事件消息

		if (dw_event->type != DWT_SIG_TX_DONE) // 等待发送成功，等待在该状态内，由中断函数转换标志位
		{
			instDone = INST_DONE_WAIT_FOR_NEXT_EVENT;
			count++;
			if (count > 50000) // 如长时间未得到中断，为避免死机，重启设备
			{
				count = 0;
				HAL_NVIC_SystemReset(); // 重启
			}
			break;
		}
		count = 0;
		instDone = INST_NOT_DONE_YET;
		inst->txu.txTimeStamp = dw_event->timeStamp;
		if (inst->previousState == TA_TXRESPONSE_SENT_TORX)
		{
			inst->previousState = TA_TXRESPONSE_WAIT_SEND;
		}
		inst->AppState = TA_RXE_WAIT; // 发送resp消息后 等待接收final消息
		message = 0;
	}
		// break; // end case TA_TX_WAIT_CONF

	case TA_RXE_WAIT: // 等待接收消息
	{
		if (inst->wait4ack == 0)
		{
			if (dwt_read16bitoffsetreg(0x19, 1) != 0x0505)
			{
				dwt_rxenable(DWT_START_RX_IMMEDIATE); // 打开接收机，等待POLL数据
			}
		}
		else
		{
			inst->wait4ack = 0;
		}

		instDone = INST_DONE_WAIT_FOR_NEXT_EVENT;
		inst->AppState = TA_RX_WAIT_DATA; // 设置下一个状态标志位TA_RX_WAIT_DATA
		if (message == 0)
		{
			break;
		}
	}

	case TA_RX_WAIT_DATA:
		switch (message)
		{
		case DWT_SIG_RX_OKAY: // 读取接收成功事件消息
		{
			event_data_t *dw_event = instance_getevent(); // 读取事件消息
			uint8 srcAddr[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // 源地址
			uint8 dstAddr[8] = {0, 0, 0, 0, 0, 0, 0, 0};  // 目标地址
			int fcode = 0;
			uint8 tof_idx = 0;
			int tag_index = 0;
			uint8 *messageData;

			memcpy(&srcAddr[0], &(dw_event->msgu.rxmsg_ss.sourceAddr[0]), ADDR_BYTE_SIZE_S); // 获取消息源地址
			memcpy(&dstAddr[0], &(dw_event->msgu.rxmsg_ss.destAddr[0]), ADDR_BYTE_SIZE_S);	 // 获取消息目标地址
			fcode = dw_event->msgu.rxmsg_ss.messageData[FCODE];								 // 获取消息功能码
			messageData = &dw_event->msgu.rxmsg_ss.messageData[0];							 // 获取消息数据
			tof_idx = srcAddr[0];

			switch (fcode) // 判断数据功能码
			{
			case RTLS_DEMO_MSG_TAG_POLL: // 标签POLL消息
			{
				tag_index = srcAddr[0] + (((uint16)srcAddr[1]) << 8);
				inst->rangeNumA[tag_index] = messageData[POLL_RNUM]; // 记录rangeNum
				inst->tagPollRxTime = dw_event->timeStamp;			 // 记录 Poll Rx 时间戳

				inst->AppState = check_state_change(dw_event->typePend); // 判断是否更换状态
				if (inst->AppState == TA_TX_WAIT_CONF)
				{
					inst->previousState = TA_TXRESPONSE_SENT_POLLRX; // wait for TX confirmation of sent response
				}
			}
			break;

			case RTLS_DEMO_MSG_ANCH_RESP:
			{
				uint8 currentRangeNum = (messageData[TOFRN] + 1); // currentRangeNum = previous + 1

				// the response has been sent - await TX done event
				if (dw_event->typePend == DWT_SIG_TX_PENDING) // anchor received response from anchor ID - 1 so is sending it's response now back to tag
				{
					inst->AppState = TA_TX_WAIT_CONF; // 设置状态机标志位，等待发送完成
					inst->previousState = TA_TXRESPONSE_SENT_RESPRX;
				}
				// already re-enabled the receiver
				else if (dw_event->typePend == DWT_SIG_RX_PENDING)
				{
					// stay in TA_RX_WAIT_DATA - receiver is already enabled.
				}
				else
				{
					inst->AppState = TA_RXE_WAIT; // wait for next frame
				}

				if (currentRangeNum == inst->rangeNum) // these are the previous ranges...
				{
					// copy the ToF and put into array (array holds last 4 ToFs)
					memcpy(&inst->tofArray[tof_idx], &(messageData[TOFR]), 4);

					// check if the ToF is valid, this makes sure we only report valid ToFs
					// e.g. consider the case of reception of response from anchor a1 (we are anchor a2)
					// if a1 got a Poll with previous Range number but got no Final, then the response will have
					// the correct range number but the range will be INVALID_TOF
					if (inst->tofArray[tof_idx] != INVALID_TOF)
					{
						inst->rxResponseMask |= (0x1 << (tof_idx));
					}
				}
				else // mark as invalid (clear the array)
				{
					if (inst->tofArray[tof_idx] != INVALID_TOF)
					{
						inst->tofArray[tof_idx] = INVALID_TOF;
					}
				}
			}
			break;

			case RTLS_DEMO_MSG_TAG_FINAL: // 标签FINAL消息
			{
				uint64 tof = INVALID_TOF;
				uint8 validResp = messageData[VRESP];
				tag_index = srcAddr[0] + (((uint16)srcAddr[1]) << 8);

				if (inst->rangeNumA[tag_index] != messageData[POLL_RNUM]) // 判断FINAL的rangeNum和POLL的rangeNum相同表示为同一组TWR测距
				{
					inst->AppState = TA_RXE_WAIT;
					break;
				}

				if (((validResp & (0x1 << (inst->shortAdd_idx))) != 0)) // 如标签收到了该基站的RESP消息，则计算TOF
				{
					tof = calc_tof(messageData, inst->txu.anchorRespTxTime, dw_event->timeStamp, inst->tagPollRxTime, inst->shortAdd_idx);
				}

				// 计算测距数值
				inst->newRangeAncAddress = inst->instanceAddress16;
				inst->delayedTRXTime32h = 0;
				inst->newRangeTagAddress = tag_index;
				inst->tof[tag_index] = tof;
				inst->newRange = instance_calc_ranges(&inst->tofArray[0], MAX_ANCHOR_LIST_SIZE, TOF_REPORT_T2A, &inst->rxResponseMask);
				inst->rxResponseMaskReport = inst->rxResponseMask; // copy the valid mask to report
				inst->rxResponseMask = 0;

				// 将标签测距数据写入标签测距数组，以备后续的数据发送和显示使用
				if (tof != INVALID_TOF)
				{
					instance_set_tagdist(tag_index, inst->shortAdd_idx);

					inst->rxResponseMask = (0x1 << inst->shortAdd_idx);
					inst->tofArray[inst->shortAdd_idx] = tof;
				}
				inst->newRangeTime = dw_event->uTimeStamp;

				if (dw_event->typePend != DWT_SIG_RX_PENDING)
				{
					inst->AppState = TA_RXE_WAIT; // 设置标志位TA_RXE_WAIT 等待下一次数据接收
				}
			}
			break; // RTLS_DEMO_MSG_TAG_FINAL

			default:
			{
				inst->AppState = TA_RXE_WAIT; // 设置标志位TA_RXE_WAIT 等待下一次数据接收
				dwt_setrxaftertxdelay(0);
			}
			break;
			} // end switch (fcode)
		}
		break; // end of DWT_SIG_RX_OKAY

		case DWT_SIG_RX_TIMEOUT: // 接收消息超时
		{
			event_data_t *dw_event = instance_getevent(); // 读取事件消息

			if (dw_event->typePend == DWT_SIG_TX_PENDING)
			{
				inst->AppState = TA_TX_WAIT_CONF;			   // wait confirmation
				inst->previousState = TA_TXRESPONSE_SENT_TORX; // wait for TX confirmation of sent response
			}
			else if (dw_event->typePend == DWT_SIG_DW_IDLE) // if timed out and back in receive then don't process as timeout
			{
				anch_process_RX_timeout(inst);
				instDone = INST_NOT_DONE_YET;
			}
			message = 0;
		}
		break;

		default:
		{
			if (message)
			{
				instance_getevent(); // 读取事件消息
			}

			if (instDone == INST_NOT_DONE_YET)
			{
				instDone = INST_DONE_WAIT_FOR_NEXT_EVENT;
			}
		}
		break;
		}
		break; // end case TA_RX_WAIT_DATA
	default:
		break;
	} // end switch on AppState

	return instDone;
} // end testapprun_anch()

/*
 * 函数名称：int anch_run(void)
 * 主要功能：
 * 基站主功能函数入口
 *
 */
int anch_run(void)
{
	instance_data_t *inst = instance_get_local_structure_ptr();
	int done = INST_NOT_DONE_YET;
	while (done == INST_NOT_DONE_YET)
	{
		done = anch_app_run(inst); // 运行状态机
	}
	return 0;
}
