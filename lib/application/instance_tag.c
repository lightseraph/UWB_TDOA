/*
 * 文件名：instance_tag.c
 * 修改日期：2019年9月19日
 * 标签TWR程序
 * 
 */
#include "compiler.h"
#include "port.h"
#include "deca_device_api.h"
#include "deca_spi.h"
#include "deca_regs.h"
#include "instance.h"

/*
 * 函数名称：void tag_enable_rx(uint32 dlyTime)
 * 主要功能：
 * 标签使能接收功能，传入参数：延时时间
 * 
 */
void tag_enable_rx(uint32 dlyTime)
{
	instance_data_t* inst = instance_get_local_structure_ptr();
	dwt_setdelayedtrxtime(dlyTime - inst->preambleDuration32h);
	if(dwt_rxenable(DWT_START_RX_DELAYED|DWT_IDLE_ON_DLY_ERR)==DWT_ERROR) //延时打开接收机
	{
		//设置延时开启接收机失败，立即开启接收
		dwt_setpreambledetecttimeout(0); //清除前导码超时
		dwt_setrxtimeout((uint16)inst->fwto4RespFrame_sy*2); //设置延时RX时间，加长超时时间以便多接收一会
		dwt_rxenable(DWT_START_RX_IMMEDIATE); //开启接收，立即开启
		//恢复设置前的参数
		dwt_setpreambledetecttimeout(PTO_PACS); //设置前导码超时时间
		dwt_setrxtimeout((uint16)inst->fwto4RespFrame_sy); //恢复设置接收超时时间为resp消息时长
	}
}

/*
 * 函数名称：void tag_process_rx_timeout(instance_data_t *inst)
 * 主要功能：
 * 接收消息超时处理函数
 * 
 */
void tag_process_rx_timeout(instance_data_t *inst)
{
#if(DISCOVERY == 1)//DISCOVERY功能
    if(inst->twrMode == GREETER)
    {
    	inst->instToSleep = TRUE;
		// initiate the re-transmission of the poll that was not responded to
		inst->AppState = TA_TXE_WAIT;
		inst->nextState = TA_TXBLINK_WAIT_SEND;
    }
    else
#endif
    {
		if(inst->rxResponseMask == 0) //如果没有一个基站回复RESP，则直接休眠，等待下一次测距周期的开启
		{
			inst->instToSleep = TRUE; 
			inst->AppState = TA_TXE_WAIT;
			inst->nextState = TA_TXPOLL_WAIT_SEND;
		}

		else if (inst->previousState == TA_TXFINAL_WAIT_SEND)  //如果没有一个基站回复RESP，则直接休眠，等待下一次测距周期的开启
		{
			dwt_forcetrxoff();	//关闭收发器，重新开始测距周期
			inst->instToSleep = TRUE;
			inst->AppState = TA_TXE_WAIT;
			inst->nextState = TA_TXPOLL_WAIT_SEND;
		}
		else //如果有至少一个基站回复RESP，则发送final
		{
			inst->AppState = TA_TXE_WAIT;
			inst->nextState = TA_TXFINAL_WAIT_SEND;
		}
    }
}

/*
 * 函数名称：uint8 tag_rx_reenable(uint16 sourceAddress, uint8 error)
 * 主要功能：
 * 用于接收多个resp的接收机重复使能
 * 
 */
uint8 tag_rx_reenable(uint16 sourceAddress, uint8 error)
{
	uint8 type_pend = DWT_SIG_DW_IDLE;
	uint8 anc = sourceAddress;
	instance_data_t* inst = instance_get_local_structure_ptr();

	switch(anc)
	{
		//接收到A3基站消息，则测距周期结束
		case 3:
			type_pend = DWT_SIG_DW_IDLE; 
			break;
		//收到A0-2基站消息，则继续等待其他基站消息
		case 0:
		case 1:
		case 2:
		default:
			if(inst->remainingRespToRx > 0) //remainingRespToRx为收到基站resp消息个数
			{
				if(error == 0)
				{
					switch (anc)
					{
						case 0:
							inst->remainingRespToRx = 3; //收到基站A0的resp则再期待3个resp消息
							break;
						case 1:
							inst->remainingRespToRx = 2; //收到基站A1的resp则再期待2个resp消息
							break;
						case 2:
							inst->remainingRespToRx = 1; //收到基站A2的resp则再期待1个resp消息
							break;
					}
				}
				//使能接收机
				tag_enable_rx(inst->tagPollTxTime32h +(MAX_ANCHOR_LIST_SIZE-inst->remainingRespToRx+1)*(inst->fixedReplyDelayAnc32h));
				type_pend = DWT_SIG_RX_PENDING;
			}
			else //无resp收到
			{
				type_pend = DWT_SIG_DW_IDLE; //进入超时处理
			}
			break;
	}
	return type_pend;
}

/*
 * 函数名称：void tag_handle_error_unknownframe(event_data_t dw_event)
 * 主要功能：
 * 收到未知消息或错误数据的处理函数
 * 
 */
void tag_handle_error_unknownframe(event_data_t dw_event)
{
	instance_data_t* inst = instance_get_local_structure_ptr();
	if(inst->twrMode != GREETER)
	{
		inst->remainingRespToRx--; 
		dw_event.typePend = tag_rx_reenable(0, 1); 
	}
	else
	{
		dw_event.typePend = DWT_SIG_DW_IDLE; 
	}
	dw_event.type = 0;
	dw_event.rxLength = 0;
	instance_putevent(dw_event, DWT_SIG_RX_TIMEOUT);
}

/*
 * 函数名称：void rx_to_cb_tag(const dwt_cb_data_t *rxd)
 * 主要功能：
 * 接收消息超时回调函数
 */
void rx_to_cb_tag(const dwt_cb_data_t *rxd)
{
	event_data_t dw_event;
    dw_event.uTimeStamp = portGetTickCnt();//记录时间，调试用
    tag_handle_error_unknownframe(dw_event);//调用tag_handle_error_unknownframe(dw_event)处理
}

/*
 * 函数名称：void rx_err_cb_tag(const dwt_cb_data_t *rxd)
 * 主要功能：
 * 接收消息错误回调函数
 */
void rx_err_cb_tag(const dwt_cb_data_t *rxd)
{
	event_data_t dw_event;
    dw_event.uTimeStamp = portGetTickCnt();//记录时间，调试用
    tag_handle_error_unknownframe(dw_event);//调用tag_handle_error_unknownframe(dw_event)处理
}

/*
 * 函数名称：void rx_err_cb_tag(const dwt_cb_data_t *rxd)
 * 主要功能：
 * 接收消息成功回调函数
 */
void rx_ok_cb_tag(const dwt_cb_data_t *rxd)
{
	instance_data_t* inst = instance_get_local_structure_ptr();
	uint8 rxTimeStamp[5]  = {0, 0, 0, 0, 0};//接收数据时间戳

    uint8 rxd_event = 0;
	uint8 fcode_index  = 0;
	uint8 srcAddr_index = 0;
	event_data_t dw_event;

    dw_event.uTimeStamp = portGetTickCnt();//记录时间，调试用
	dw_event.rxLength = rxd->datalength;

	//校验frame control为0X41 0X88，标准数据帧头格式，短地址模式
	if(rxd->fctrl[0] == 0x41)
	{
		if((rxd->fctrl[1] & 0xCC) == 0x88) 
		{
			fcode_index = FRAME_CRTL_AND_ADDRESS_S;
			srcAddr_index = FRAME_CTRLP + ADDR_BYTE_SIZE_S;
			rxd_event = DWT_SIG_RX_OKAY;//设置接收成功事件标志位
		}
#if (DISCOVERY == 1)  //DISCOVERY模式的bilnk会使用8byte的长地址
		else if((rxd->fctrl[1] & 0xCC) == 0x8c) //long/short address - ranging init message
		{
			fcode_index = FRAME_CRTL_AND_ADDRESS_LS; //function code is in first byte after source address
			srcAddr_index = FRAME_CTRLP + ADDR_BYTE_SIZE_L;
			rxd_event = DWT_SIG_RX_OKAY;
		}
#endif
		else
		{
			rxd_event = SIG_RX_UNKNOWN; //错误消息头
		}
	}
	else
	{
		rxd_event = SIG_RX_UNKNOWN; //错误消息头
	}

	dwt_readrxtimestamp(rxTimeStamp); //读取接收时间戳
	dwt_readrxdata((uint8 *)&dw_event.msgu.frame[0], rxd->datalength, 0);  //读取接收数据
	instance_seteventtime(&dw_event, rxTimeStamp); //接收时间接入事件消息

	dw_event.type = 0; 
	dw_event.typePend = DWT_SIG_DW_IDLE;

	if(rxd_event == DWT_SIG_RX_OKAY) //正确接收消息
	{
		uint16 sourceAddress = (((uint16)dw_event.msgu.frame[srcAddr_index+1]) << 8) + dw_event.msgu.frame[srcAddr_index];//获取源地址
		switch(dw_event.msgu.frame[fcode_index]) //识别功能码
		{
			case RTLS_DEMO_MSG_ANCH_RESP:  //当前消息是基站的resp消息
			{
				if(inst->twrMode == INITIATOR)
				{
					uint8 index;
					inst->remainingRespToRx--;
					dw_event.typePend = tag_rx_reenable(sourceAddress, 0); //打开接收机等待下一个基站的resep消息
					index = RRXT0 + 5*(sourceAddress);  //从final消息的第7个字节写入respRX时间戳，每个时间戳占5个字节
					inst->rxResponseMask |= (0x1 << (sourceAddress)); //正确接收resp则写入rxResponseMask校验位
					memcpy(&(inst->msg_f.messageData[index]), rxTimeStamp, 5); //将基站resp接收时间戳写入final消息中
					break;
				}
			}
#if (DISCOVERY == 1)
			case RTLS_DEMO_MSG_RNG_INIT:
			{
				if(inst->twrMode == GREETER)
				{
					rxd_event = RTLS_DEMO_MSG_RNG_INIT;
					break; //process the event in the application
				}
			}
#endif
			case RTLS_DEMO_MSG_TAG_POLL:
			case RTLS_DEMO_MSG_TAG_FINAL:
			default:
			//忽略其他消息，只处理基站RESP消息
			{
				tag_handle_error_unknownframe(dw_event);
				return;
			}
		}
		instance_putevent(dw_event, rxd_event);//写入事件消息
	}
	else
	{
		tag_handle_error_unknownframe(dw_event);
	}
	
}

/*
 * 函数名称：int tag_app_run(instance_data_t *inst)
 * 主要功能：
 * 主功能函数入口，在此函数内循环执行状态机控制系统功能流程
 */
int tag_app_run(instance_data_t *inst)
{
	int instDone = INST_NOT_DONE_YET; //实例结束标志
    int message = instance_peekevent(); //读取事件消息

    switch (inst->AppState)  //状态机入口，通过判断inst->AppState确定进入哪个流程中
    {
        case TA_INIT:  //标签初始化状态，设置初始化参数
		{
			uint16 sleep_mode = 0;//初始化sleep_mode=0
			dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN); //设置帧过滤模式，允许接收帧数据和ACK数据
			memcpy(inst->eui64, &inst->instanceAddress16, ADDR_BYTE_SIZE_S); //将标签EUI的最后2个字节设置为标签短地址，如T1标签的EUI=[FF FF FF FF 00 00 00 01]
			
			dwt_seteui(inst->eui64);//写入寄存器，设置标签EUI
			dwt_setpanid(inst->panID);//写入寄存器，设置标签PANID
#if (DISCOVERY == 1)  //多标签DISCOVERY模式
			//Start off by sending Blinks and wait for Anchor to send Ranging Init
			inst->AppState = TA_TXBLINK_WAIT_SEND;
			inst->tagSleepTime_ms = BLINK_PERIOD;
			memcpy(inst->blinkmsg.tagID, inst->eui64, ADDR_BYTE_SIZE_L);
			inst->newRangeTagAddress = inst->eui64[1];
			inst->newRangeTagAddress = (inst->newRangeTagAddress << 8) + inst->eui64[0];
#else
			inst->newRangeTagAddress = inst->instanceAddress16;
			dwt_setaddress16(inst->instanceAddress16);//设置短地址
			inst->nextState = TA_TXPOLL_WAIT_SEND;//设置下一个状态为TA_TXPOLL_WAIT_SEND
			inst->AppState = TA_TXE_WAIT;//设置状态机为TA_TXE_WAIT
			inst->instToSleep = TRUE;//启动后先进入休眠模式，设置为TRUE
			inst->tagSleepTime_ms = inst->tagPeriod_ms;//标签休眠的时间设置为一个测距周期
#endif
			inst->rangeNum = 0;//初始化rangeNum=0
			inst->tagSleepCorrection_ms = 0;//初始化tagSleepCorrection_ms=0，标签休眠进入对应的SLOT校准值
			//以下设置默认休眠模式
			sleep_mode = (DWT_PRESRV_SLEEP|DWT_CONFIG|DWT_TANDV);
			if(inst->configData.txPreambLength == DWT_PLEN_64)  
			{	
				sleep_mode |= DWT_LOADOPSET;
			}
#if (DEEP_SLEEP == 1)
			dwt_configuresleep(sleep_mode, DWT_WAKE_WK|DWT_WAKE_CS|DWT_SLP_EN); //configure the on wake parameters (upload the IC config settings)
#endif

			instance_config_frameheader_16bit(inst);//设置数据帧格式FC控制字节
			inst->instanceWakeTime_ms = portGetTickCnt();//记录当前系统时间
		}
        break; // end case TA_INIT

        case TA_SLEEP_DONE:  //在该状态内等待休眠结束
		{
			
			event_data_t* dw_event = instance_getevent(); //获取事件信息
			if (dw_event->type != DWT_SIG_RX_TIMEOUT)//关键字instance_putevent(dw_event, DWT_SIG_RX_TIMEOUT);设置到时事件
			{
				//未到唤醒时间则在此等待休眠状态结束
				instDone = INST_DONE_WAIT_FOR_NEXT_EVENT;
				break;//未到唤醒时间则每次进来都break，不会向下运行
			}

			instDone = INST_NOT_DONE_YET;
			inst->instToSleep = FALSE;//取消休眠
			inst->AppState = inst->nextState; //设置下一个状态标志
			inst->nextState = 0; //clear
			inst->instanceWakeTime_ms = portGetTickCnt(); //记录唤醒时间
			led_on(LED_PC9);
#if (DEEP_SLEEP == 1)
			{
				//唤醒DWM1000
				port_wakeup_dw1000_fast();

				//设置收发LED闪烁模式
				dwt_setleds(1);
				//设置天线延迟
				dwt_settxantennadelay(inst->txAntennaDelay);
#if(DISCOVERY == 0)
				dwt_seteui(inst->eui64); //设置EUI
#endif
			}
#else
            Sleep(2); 
#endif
			
		}
        break;

        case TA_TXE_WAIT: //发送消息或进入休眠
#if(DISCOVERY == 1)
        	if(((inst->nextState == TA_TXPOLL_WAIT_SEND)||(inst->nextState == TA_TXBLINK_WAIT_SEND))&& (inst->instToSleep))
#else
        	if((inst->nextState == TA_TXPOLL_WAIT_SEND)&& (inst->instToSleep)) //如需先进行休眠
#endif
            //在新的测距周期开始前，先进入睡眠模式   
            {
            	inst->rangeNum++; //rangeNum自增+1
                //程序将进入休眠状态直到tagSleepTime_ms后唤醒
				instDone = INST_DONE_WAIT_FOR_NEXT_EVENT_TO; //don't sleep here but kick off the Sleep timer countdown
                inst->AppState = TA_SLEEP_DONE;//设置状态机标志位为TA_SLEEP_DONE
                {
#if (DEEP_SLEEP == 1)
					dwt_entersleep(); //进入低功耗睡眠模式
#endif					
					if(inst->rxResponseMask != 0)//将上个测距周期的resp消息计算出TOF
					{
						//标签将resp消息内的TOF(n-1)计算和输出
						inst->newRange = instance_calc_ranges(&inst->tofArray[0], MAX_ANCHOR_LIST_SIZE, TOF_REPORT_T2A, &inst->rxResponseMask);
						inst->rxResponseMaskReport = inst->rxResponseMask;
						inst->rxResponseMask = 0;
						inst->newRangeTime = portGetTickCnt();
					}
                }
            }
            else //不休眠，进行数据传输
            {
                inst->AppState = inst->nextState;
                inst->nextState = 0; 
            }
            break; // end case TA_TXE_WAIT
        case TA_TXBLINK_WAIT_SEND://发送blink消息，DISCOVERY模式用
            {
				int flength = (BLINK_FRAME_CRTL_AND_ADDRESS + FRAME_CRC);

                //blink frames with IEEE EUI-64 tag ID
                inst->blinkmsg.frameCtrl = 0xC5;
                inst->blinkmsg.seqNum = inst->frameSN++;

				dwt_writetxdata(flength, (uint8 *)  (&inst->blinkmsg), 0);	// write the frame data
				dwt_writetxfctrl(flength, 0, 1);

				inst->twrMode = GREETER;
				//using wait for response to do delayed receive
				inst->wait4ack = DWT_RESPONSE_EXPECTED;
				inst->rxResponseMask = 0;

				dwt_setrxtimeout((uint16)inst->fwto4RespFrame_sy*2);  //units are symbols (x2 as ranging init > response)
				//set the delayed rx on time (the ranging init will be sent after this delay)
				dwt_setrxaftertxdelay((uint32)inst->tagRespRxDelay_sy);  //units are 1.0256us - wait for wait4respTIM before RX on (delay RX)

				dwt_starttx(DWT_START_TX_IMMEDIATE | inst->wait4ack); //always using immediate TX and enable delayed RX

				inst->instToSleep = 1; //go to Sleep after this blink
                inst->AppState = TA_RX_WAIT_DATA; // to to RX, expecting ranging init response
                inst->previousState = TA_TXBLINK_WAIT_SEND;
                instDone = INST_DONE_WAIT_FOR_NEXT_EVENT; //will use RX FWTO to time out (set below)

            }
            break; // end case TA_TXBLINK_WAIT_SEND

        case TA_TXPOLL_WAIT_SEND://将要发送的POLL数据进行打包和发送
		{
			inst->msg_f.messageData[POLL_RNUM] = inst->rangeNum; //POLL数据协议中的rangeNum字节赋值
			inst->msg_f.messageData[FCODE] = RTLS_DEMO_MSG_TAG_POLL; //POLL数据协议中的FunctionCode字节赋值
			inst->psduLength = (TAG_POLL_MSG_LEN + FRAME_CRTL_AND_ADDRESS_S + FRAME_CRC);//计算POLL消息长度
			inst->msg_f.seqNum = inst->frameSN++; //MAC数据协议中的sequence number赋值
			inst->msg_f.sourceAddr[0] = inst->instanceAddress16 & 0xff; //POLL数据协议中的源地址，即自己的16bit短地址
			inst->msg_f.sourceAddr[1] = (inst->instanceAddress16>>8) & 0xff; 
			inst->msg_f.destAddr[0] = 0xff;  //POLL数据协议中的目标地址，POLL消息是广播，则为0xFFFF
			inst->msg_f.destAddr[1] = 0xff;  
			dwt_writetxdata(inst->psduLength, (uint8 *)  &inst->msg_f, 0);//将数据帧写入相应寄存器	
			dwt_setrxaftertxdelay((uint32)inst->tagRespRxDelay_sy);//设置发送后延时tagRespRxDelay_sy打开接收机进行A0的resp消息接收  
			inst->remainingRespToRx = MAX_ANCHOR_LIST_SIZE; //期望收到4个RESP消息
			dwt_setrxtimeout((uint16)inst->fwto4RespFrame_sy); //设置接收超时时间
			dwt_setpreambledetecttimeout(PTO_PACS); //设置前导码超时时间
			inst->rxResponseMask = 0;	//复位接收到resp的标志字节
			inst->wait4ack = DWT_RESPONSE_EXPECTED; //设置发送消息后等待接收，则系统将自动在发射后开启接收机等待接收数据
			dwt_writetxfctrl(inst->psduLength, 0, 1); //设置发送数据帧控制格式
			inst->twrMode = INITIATOR;//设置当前板卡TWR模式，POLL发起者为INITIATOR
			dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED); //控制消息发出

			inst->AppState = TA_TX_WAIT_CONF;  //设置状态标志位为TA_TX_WAIT_CONF
			inst->previousState = TA_TXPOLL_WAIT_SEND; //设置上一个状态为TA_TXPOLL_WAIT_SEND
			instDone = INST_DONE_WAIT_FOR_NEXT_EVENT; //当前事件处理完毕，可进行下一个事件的处理过程
		}
		break;

        case TA_TXFINAL_WAIT_SEND://打包并发送FINAL消息
		{
			//inst->msg_f.messageData为消息数据数组
			inst->msg_f.messageData[POLL_RNUM] = inst->rangeNum;//设置rangeNum
			inst->msg_f.messageData[VRESP] = inst->rxResponseMask;//设置rxResponseMask，标记哪几个resp消息有效
			inst->msg_f.messageData[FCODE] = RTLS_DEMO_MSG_TAG_FINAL; //设置function code
			inst->psduLength = (TAG_FINAL_MSG_LEN + FRAME_CRTL_AND_ADDRESS_S + FRAME_CRC);//设置数据长度
			inst->msg_f.seqNum = inst->frameSN++; //设置消息seqNum
			dwt_writetxdata(inst->psduLength, (uint8 *)&inst->msg_f, 0); //将数据写入寄存器
			inst->wait4ack = 0; //清除标志位

			if(instance_send_delayed_frame(inst, DWT_START_TX_DELAYED))  //延时发送
			{
				//发送失败
				inst->AppState = TA_TXE_WAIT; //进入TA_TXE_WAIT 
				inst->nextState = TA_TXPOLL_WAIT_SEND;
				inst->instToSleep = TRUE;//开启休眠，重新一个测距周期
				break; 
			}
			else//发送成功
			{
				inst->AppState = TA_TX_WAIT_CONF;   //等待发送过程中，进入TA_TX_WAIT_CONF，发送成功后也将开启新的测距周期
			}

			inst->previousState = TA_TXFINAL_WAIT_SEND;
			inst->instToSleep = TRUE;
			instDone = INST_DONE_WAIT_FOR_NEXT_EVENT; 
		}
		break;

        case TA_TX_WAIT_CONF: //等待发送数据成功并进入下一个状态
		{
			static uint32 count=0;
			event_data_t* dw_event = instance_getevent(); //读取事件消息
			/*
				1、发送完成并成功后会产生中断，中断后进入回调函数TX_conf_cb()
				2、回调函数内处理中断，写入事件标志位
				3、此处读取事件标志位获取发送成功标志位状态
			*/
			if(dw_event->type != DWT_SIG_TX_DONE) //读取发送完成事件消息
			{
				instDone = INST_DONE_WAIT_FOR_NEXT_EVENT;//未发送完成则退出，直到发送完成后向下执行，在中断程序内改变该标志位
				count++;
				if(count>50000)//如长时间未得到中断，为避免死机，重启设备
				{
					count=0;
					HAL_NVIC_SystemReset();//重启 
				}
				break;
			}
			led_off(LED_PC9);
			count=0;
			instDone = INST_NOT_DONE_YET;
			//判断上一个状态标识位即从哪个状态来的当前状态
			if(inst->previousState == TA_TXFINAL_WAIT_SEND)  //上一个状态标志位是TA_TXFINAL_WAIT_SEND
			{
				inst->AppState = TA_TXE_WAIT;
				inst->nextState = TA_TXPOLL_WAIT_SEND;
				break;
			}
			else  //from TA_TXPOLL_WAIT_SEND
			{
				inst->txu.txTimeStamp = dw_event->timeStamp;  //记录数据发送时间戳
				inst->tagPollTxTime32h = dw_event->timeStamp32h;  //记录poll数据发送时间戳

				if(inst->previousState == TA_TXPOLL_WAIT_SEND)//上一个状态标志位是TA_TXPOLL_WAIT_SEND
				{
					uint64 tagCalculatedFinalTxTime; //定义final发送时间戳
					//final发送的时间=当前时间+固定的poll到final的时间延时
					tagCalculatedFinalTxTime =  (inst->txu.txTimeStamp + inst->pollTx2FinalTxDelay) & MASK_TXDTS;
					inst->delayedTRXTime32h = tagCalculatedFinalTxTime >> 8; //delayedTRXTime32h为设置发送消息后延时某时间后再次发送的延时时间
					tagCalculatedFinalTxTime = tagCalculatedFinalTxTime + inst->txAntennaDelay;//添加天线延时时间
					tagCalculatedFinalTxTime &= MASK_40BIT;
					/*
						因标签的final消息中有final_TX的时间戳，而如果发送完final再计算时间戳就不会在final里发出了，此处用的机制是：
						1、先固定从pollTX到finalTX的时间为固定时间T
						2、发送完poll后设置dw1000这个固定时间T后发送fianl
						3、把T写入final消息中
					*/
					//将final的TX时间戳写入final消息中
					memcpy(&(inst->msg_f.messageData[FTXT]), (uint8 *)&tagCalculatedFinalTxTime, 5);
					//将poll的TX时间戳写入final消息中
					memcpy(&(inst->msg_f.messageData[PTXT]), (uint8 *)&inst->txu.tagPollTxTime, 5);
				}
				inst->AppState = TA_RX_WAIT_DATA;  //设置状态机标志位为TA_RX_WAIT_DATA，等待接收消息
				message = 0;//清空message，等待读取
				
			}
		}
		//break; //end case TA_TX_WAIT_CONF

        case TA_RX_WAIT_DATA: //等待接收消息
            switch (message)
            {
                case DWT_SIG_RX_OKAY://收到数据帧
                {
					event_data_t* dw_event = instance_getevent(); //接收事件消息
					uint8  srcAddr[8] = {0,0,0,0,0,0,0,0};
					uint8  dstAddr[8] = {0,0,0,0,0,0,0,0};
                    int fcode = 0;
					uint8 tof_idx  = 0;
					uint8 *messageData;

					memcpy(&srcAddr[0], &(dw_event->msgu.rxmsg_ss.sourceAddr[0]), ADDR_BYTE_SIZE_S);  //读取源地址
					memcpy(&dstAddr[0], &(dw_event->msgu.rxmsg_ss.destAddr[0]), ADDR_BYTE_SIZE_S);    //读取目标地址
					fcode = dw_event->msgu.rxmsg_ss.messageData[FCODE];     //读取function code
					messageData = &dw_event->msgu.rxmsg_ss.messageData[0];  //读取消息内容
					tof_idx = srcAddr[0];  
					
					switch(fcode)  //根据功能码判断消息类型和对消息进行处理和解析
					{
						case RTLS_DEMO_MSG_ANCH_RESP:  //收到基站发送的resp消息
						{
							uint8 currentRangeNum = (messageData[TOFRN] + 1); //计算RangeNum
							if(GATEWAY_ANCHOR_ADDR == (srcAddr[0] | ((uint32)(srcAddr[1] << 8)))) //收到A0(主基站)的resp消息
							{
								/*
									A0基站具有SleepCorrection校准功能，此处接收校准信息
									tagSleepRnd_ms是未收到A0校准信息的默认测距周期，如收到A0消息后，将tagSleepRnd_ms清零不再使用
								*/
								inst->tagSleepCorrection_ms = (int16) (((uint16) messageData[RES_TAG_SLP1] << 8) + messageData[RES_TAG_SLP0]);
								inst->tagSleepRnd_ms = 0; 
							}

							if(dw_event->typePend == DWT_SIG_RX_PENDING)
							{
								//继续在TA_RX_WAIT_DATA等待，接收机当前已经开启，等待所有resp消息的发送
							}
							else //(dw_event->type_pend == DWT_SIG_DW_IDLE) 接收完成
							{								
								inst->AppState = TA_TXFINAL_WAIT_SEND; //发送final消息
							}

							if(currentRangeNum == inst->rangeNum) 
							{
								//将基站发送的resp消息内的TOF(n-1)接收并保存
								memcpy(&inst->tofArray[tof_idx], &(messageData[TOFR]), 4);
								//校验TOF正确并对应到相应基站编号
								if(inst->tofArray[tof_idx] != INVALID_TOF)
								{
									inst->rxResponseMask |= (0x1 << tof_idx);
								}
							}
							else
							{
								if(inst->tofArray[tof_idx] != INVALID_TOF)
								{
									inst->tofArray[tof_idx] = INVALID_TOF;
								}
							}
						}
						break; //end  RTLS_DEMO_MSG_ANCH_RESP

						default:
						{
							tag_process_rx_timeout(inst); //如消息未知则进入timeout处理环节
						}
						break;
					} //end switch (fcode)

                }
				break; //end of DWT_SIG_RX_OKAY
#if(DISCOVERY == 1)
                case RTLS_DEMO_MSG_RNG_INIT:  //测距初始化数据，当DISCOVERY=1时会产生该消息
                {
                	event_data_t* dw_event = instance_getevent(); //get and clear this event
                	uint8  srcAddr[8] = {0,0,0,0,0,0,0,0};

                	uint8* messageData = &dw_event->msgu.rxmsg_ls.messageData[0];
                	memcpy(&srcAddr[0], &(dw_event->msgu.rxmsg_ls.sourceAddr[0]), ADDR_BYTE_SIZE_S);

					if(GATEWAY_ANCHOR_ADDR == (srcAddr[0] | ((uint32)(srcAddr[1] << 8)))) //if response from gateway then use the correction factor
					{
						// casting received bytes to int because this is a signed correction -0.5 periods to +1.5 periods
						inst->tagSleepCorrection_ms = (int16) (((uint16) messageData[RES_TAG_SLP1] << 8) + messageData[RES_TAG_SLP0]);
						inst->tagSleepRnd_ms = 0; // once we have initial response from Anchor #0 the slot correction acts and we don't need this anymore
					}

					//get short address from anchor
					inst->instanceAddress16 = (int16) (((uint16) messageData[RES_TAG_ADD1] << 8) + messageData[RES_TAG_ADD0]);

					//set source address
					inst->newRangeTagAddress = inst->instanceAddress16;
					dwt_setaddress16(inst->instanceAddress16);

					inst->nextState = TA_TXPOLL_WAIT_SEND;
					inst->AppState = TA_TXE_WAIT;
					inst->instToSleep = TRUE;

					inst->tagSleepTime_ms = inst->tagPeriod_ms;

					break; //RTLS_DEMO_MSG_RNG_INIT
                }
#endif

                case DWT_SIG_RX_TIMEOUT: //接收resp消息超时
				{
					event_data_t* dw_event = instance_getevent(); //获取事件信息
					//Anchor can time out and then need to send response - so will be in TX pending
					if(dw_event->typePend == DWT_SIG_TX_PENDING) //当处于DWT_SIG_TX_PENDING发送状态不明确时，重新进入TA_TX_WAIT_CONF状态，进行发送控制
					{
						inst->AppState = TA_TX_WAIT_CONF;                  
						inst->previousState = TA_TXRESPONSE_SENT_TORX;   
					}
					else if(dw_event->typePend == DWT_SIG_DW_IDLE) //芯片处于空闲状态为收到resp消息
					{
						tag_process_rx_timeout(inst); //进入接收超时处理
					}
					message = 0; //清零message
				}
                break;

                default: //未知消息或无消息
                {
                    if(message) //未知消息则继续在该状态内等待正确消息的到来
                    {
                    	instDone = INST_DONE_WAIT_FOR_NEXT_EVENT;
                    }

                	if(instDone == INST_NOT_DONE_YET)  //无消息则继续在该状态内等待消息的到来
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
} // end testapprun_tag()

/*
 * 函数名称：int tag_run(void)
 * 主要功能：
 * 标签主功能入口
 */
int tag_run(void)
{
	instance_data_t* inst = instance_get_local_structure_ptr();
    int done = INST_NOT_DONE_YET;

    while(done == INST_NOT_DONE_YET)
	{
		done = tag_app_run(inst); //进入标签测距周期状态机
	}

    if(done == INST_DONE_WAIT_FOR_NEXT_EVENT_TO) //标签完成一次测距周期，进入休眠
    {
		int32 nextPeriod;//设置下次测距周期开始时间=休眠时间+校准时间
		nextPeriod = inst->tagSleepRnd_ms + inst->tagSleepTime_ms + inst->tagSleepCorrection_ms;
		inst->nextWakeUpTime_ms = (uint32) nextPeriod; //设置唤醒时间
		inst->tagSleepCorrection_ms = 0; //清除tagSleepCorrection_ms
		inst->instanceTimerEn = 1; //开始计时器计时
    }

    if(inst->instanceTimerEn == 1)
    {
		if((portGetTickCnt() - inst->instanceWakeTime_ms) > inst->nextWakeUpTime_ms)  //休眠时间到
		{
			event_data_t dw_event; //初始化事件消息
			inst->instanceTimerEn = 0;
			dw_event.rxLength = 0;
			dw_event.type = 0;
			instance_putevent(dw_event, DWT_SIG_RX_TIMEOUT);//设置事件，标记测距周期时间到，准备进入新测距周期
		}
    }
    return 0;
}
