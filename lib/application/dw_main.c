/*
 * 文件名：dw_main.c
 * 修改日期：2019年9月21日
 * 主程序入口
 *
 */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-overflow="
#endif

#include "compiler.h"
#include "port.h"
#include "instance.h"
#include "deca_types.h"
#include "deca_spi.h"
#include "iwdg.h"
#include "ssd1306.h"
#include "usart.h"
#include "kalman.h"
#include "trilateration.h"

// 函数声明
uint32 inittestapplication(uint8 s1switch);

volatile uint8_t s1switch = 0;
uint8_t instance_anchaddr = 0;
uint8_t dr_mode = 0;
uint8_t tagaddr, ancaddr;
uint8_t instance_mode = ANCHOR;

#define LCD_BUFF_LEN (20)
char lcd_data[LCD_BUFF_LEN];
uint8_t max_tag_num = 4;
uint8_t UART_RX_BUF[1];

// 默认使用110K，channel2
instanceConfig_t chConfig[2] = {
	// mode 1 - SW: 2 off 850K ch5
	{
		.channelNumber = 5,			  // channel
		.preambleCode = 4,			  // preambleCode
		.pulseRepFreq = DWT_PRF_16M,  // prf
		.dataRate = DWT_BR_850K,	  // datarate
		.preambleLen = DWT_PLEN_1024, // preambleLength
		.pacSize = DWT_PAC32,		  // pacSize
		.nsSFD = 1,					  // non-standard SFD
		.sfdTO = (1025 + 64 - 32)	  // SFD timeout		preamble length + 1 + SFD length – PAC size
	},
	// mode 2 - SW: 2 on 6.8M ch5
	{
		.channelNumber = 5,			 // channel
		.preambleCode = 4,			 // preambleCode
		.pulseRepFreq = DWT_PRF_16M, // prf
		.dataRate = DWT_BR_6M8,		 // datarate
		.preambleLen = DWT_PLEN_128, // preambleLength
		.pacSize = DWT_PAC8,		 // pacSize
		.nsSFD = 0,					 // non-standard SFD
		.sfdTO = (129 + 8 - 8)		 // SFD timeout		preamble length + 1 + SFD length – PAC size
	},
};

// 设置SLOT
sfConfig_t sfConfig[2] = {
// mode 1 - SW: 2 off 110K ch2 4tags 112ms
#define SLOT_TIME_110K 28
#define SLOT_TIME_850K 20
#define SLOT_TIME_6M8 10
	{
		.slotDuration_ms = (SLOT_TIME_850K),			 // slot duration in milliseconds (NOTE: the ranging exchange must be able to complete in this time
		.numSlots = (MAX_TAG_850K),						 // number of slots in the superframe (8 tag slots and 2 used for anchor to anchor ranging),
		.sfPeriod_ms = (MAX_TAG_850K * SLOT_TIME_850K),	 // in ms => 280ms frame means 3.57 Hz location rate
		.tagPeriod_ms = (MAX_TAG_850K * SLOT_TIME_850K), // tag period in ms (sleep time + ranging time)
		.pollTxToFinalTxDly_us = (10000)				 // poll to final delay in microseconds (needs to be adjusted according to lengths of ranging frames)
	},
#if (DISCOVERY == 1)
	// mode 2 - SW: 2 on
	{
		.slotDuration_ms = (10),		// slot duration in milliseconds (NOTE: the ranging exchange must be able to complete in this time
										// e.g. tag sends a poll, 4 anchors send responses and tag sends the final + processing time
		.numSlots = (100),				// number of slots in the superframe (98 tag slots and 2 used for anchor to anchor ranging),
		.sfPeriod_ms = (10 * 100),		// in ms => 1000 ms frame means 1 Hz location rate
		.tagPeriod_ms = (10 * 100),		// tag period in ms (sleep time + ranging time)
		.pollTxToFinalTxDly_us = (2500) // poll to final delay in microseconds (needs to be adjusted according to lengths of ranging frames)

	}
#else
	// mode 2 - SW: 2 on 6.8M ch2 15tags 150ms
	{
		.slotDuration_ms = (SLOT_TIME_6M8),			   // slot duration in milliseconds (NOTE: the ranging exchange must be able to complete in this time
		.numSlots = (MAX_TAG_68M),					   // number of slots in the superframe (8 tag slots and 2 used for anchor to anchor ranging),
		.sfPeriod_ms = (MAX_TAG_68M * SLOT_TIME_6M8),  // in ms => 100 ms frame means 10 Hz location rate
		.tagPeriod_ms = (MAX_TAG_68M * SLOT_TIME_6M8), // tag period in ms (sleep time + ranging time)
		.pollTxToFinalTxDly_us = (2500)				   // poll to final delay in microseconds (needs to be adjusted according to lengths of ranging frames)

	}
#endif

};

/*
 * 函数名称：void addressconfigure(uint8 s1switch, uint8 mode)
 * 主要功能：
 * 设置设备短地址
 */
void addressconfigure(uint8 s1switch, uint8 mode)
{
	uint16 instAddress;
	instance_anchaddr = (((s1switch & SWS1_A1A_MODE) << 2) + (s1switch & SWS1_A2A_MODE) + ((s1switch & SWS1_A3A_MODE) >> 2)) >> 4;
	// instance_anchaddr=11;
	if (mode == ANCHOR)
	{
		instAddress = GATEWAY_ANCHOR_ADDR | instance_anchaddr;
	}
	else
	{
		instAddress = instance_anchaddr;
	}
	instance_set_16bit_address(instAddress);
}

/*
 * 函数名称：int decarangingmode(uint8 s1switch)
 * 主要功能：
 * 根据拨码开关读取工作模式
 */
int decarangingmode(uint8 s1switch)
{
	int mode = 0;
	if (s1switch & SWS1_SHF_MODE)
	{
		mode = 1;
	}
	return mode;
}

/*
 * 函数名称：uint32 inittestapplication(uint8 s1switch)
 * 主要功能：
 * dw1000初始化
 */
uint32 inittestapplication(uint8 s1switch)
{
	uint32 devID;
	int result;

	devID = instance_readdeviceid(); // 读取设备ID
	if (DWT_DEVICE_ID != devID)		 // 如读取失败，先执行唤醒
	{
		port_wakeup_dw1000(); // 使用SPI-NS管脚唤醒DW1000
		devID = instance_readdeviceid();
		if (DWT_DEVICE_ID != devID) // SPI异常，或硬件故障
		{
			return (-1); // 返回错误
		}
		dwt_softreset(); // 软件复位
	}
	reset_DW1000(); // 复位

	if ((s1switch & SWS1_ANC_MODE) == 0) // 读取设备角色
	{
		instance_mode = TAG;
	}
	else
	{
		instance_mode = ANCHOR;
	}

	result = instance_init(instance_mode); // 设置工作模式
	if (0 > result)
	{
		return (-1);
	}

	port_set_dw1000_fastrate();		 // SPI高速模式
	devID = instance_readdeviceid(); // 读取设备ID

	if (DWT_DEVICE_ID != devID) // SPI异常，或硬件故障
	{
		return (-1);
	}

	addressconfigure(s1switch, instance_mode); // 设置设备短地址
	dr_mode = decarangingmode(s1switch);	   // 读取拨码开关第2位，on=6.8M,15tags,28*4ms   off=110K,4tags,10*15ms
	instance_config(&chConfig[dr_mode], &sfConfig[dr_mode], s1switch);
	if (dr_mode == 1)
	{
		max_tag_num = MAX_TAG_68M;
	}
	else
	{
		max_tag_num = MAX_TAG_850K;
	}
	return devID;
}

/*
 * 函数名称：void setLCDline1(uint8 s1switch)
 * 主要功能：
 * OLED显示
 */
#if (OLED == 1)
void setLCDline1(uint8 s1switch)
{
	int role = instance_get_role();

	sprintf((char *)&lcd_data[0], "%s%s%s", (s1switch & SWS1_KAM_MODE) ? "K-" : "  ", (s1switch & SWS1_SHF_MODE) ? "F-" : "L-", (s1switch & SWS1_HPR_MODE) ? "30dBm" : "20dBm");
	LCD_DISPLAY(55, 48, lcd_data);
	tagaddr = instance_anchaddr;
	ancaddr = instance_anchaddr;

	if (role == TAG)
	{
		sprintf((char *)&lcd_data[0], "Tag:%d", tagaddr);
		LCD_DISPLAY(0, 48, lcd_data);
	}
	else if (role == ANCHOR)
	{
		sprintf((char *)&lcd_data[0], "Anc:%d", ancaddr);
		LCD_DISPLAY(0, 48, lcd_data);
	}
	else
	{
		sprintf((char *)&lcd_data[0], "Reserved ");
		LCD_DISPLAY(0, 48, lcd_data);
	}
}
#endif

/*
 * 函数名称：int dw_main(void)
 * 主要功能
 * 主功能函数入口
 */

int dw_main(void)
{
	int rx = 0;
	int toggle = 0;
	uint8_t A0_count = 0, A1_count = 0, A2_count = 0, A3_count = 0;

	uint64_t printLCDTWRReports = 0;
	uint64_t NanTWRReports = 0;

	uint8_t UART_TX_DATA[512];
	uint8_t UART_TX_DATA2[256];
	char Location_char[30] = {0};

	port_DisableEXT_IRQ();

	s1switch = portB_is_switch_on(TA_SW1_2) << 1 // 读取拨码开关
			   | portC_is_switch_on(TA_SW1_3) << 2 | portC_is_switch_on(TA_SW1_4) << 3 | portC_is_switch_on(TA_SW1_5) << 4 | portC_is_switch_on(TA_SW1_6) << 5 | portC_is_switch_on(TA_SW1_7) << 6 | portC_is_switch_on(TA_SW1_8) << 7;

	if (inittestapplication(s1switch) == (uint32)-1) // 初始化dw1000错误
	{
		led_on(LED_ALL); // led_常亮kalman_filter
#if (OLED == 1)
		memset(lcd_data, ' ', LCD_BUFF_LEN);
		memcpy(lcd_data, (const uint8 *)"ERROR   ", 12);
		LCD_DISPLAY(0, 16, lcd_data);
		memcpy(lcd_data, (const uint8 *)"  INIT FAIL ", 12);
		LCD_DISPLAY(0, 32, lcd_data);
#endif
		return 0;
	}

	kalman_filter_Init(); // 初始化卡尔曼滤波赋初值
	led_off(LED_ALL);
	led_on(LED_PC8);

#if (OLED == 1)
	memset(lcd_data, ' ', LCD_BUFF_LEN);
	setLCDline1(s1switch);
#endif
	port_EnableEXT_IRQ(); // 使能中断

	while (1)
	{
		HAL_IWDG_Refresh(&hiwdg);
		int UART_TX_DATA_len = 0;
		int UART_TX_DATA_len2 = 0;
		instance_data_t *inst = instance_get_local_structure_ptr();
		int monitor_local = inst->monitor;
		int txdiff = (portGetTickCnt() - inst->timeofTx);
		instance_mode = instance_get_role();

		if (instance_mode == TAG) // 根据角色进入相应的状态机流程
		{
			tag_run();
		}
		else
		{
			anch_run();
		}

		// if delayed TX scheduled but did not happen after expected time then it has failed... (has to be < slot period)
		// if anchor just go into RX and wait for next message from tags/anchors
		// if tag handle as a timeout
		if ((monitor_local == 1) && (txdiff > inst->slotDuration_ms))
		{
			inst->wait4ack = 0;
			if (instance_mode == TAG)
			{
				tag_process_rx_timeout(inst);
			}
			else
			{
				dwt_forcetrxoff();
				inst->AppState = TA_RXE_WAIT;
			}
			inst->monitor = 0;
		}

		// 有新的测距数据则完成串口发送和显示
		rx = instance_newrange();
		if (rx != TOF_REPORT_NUL)
		{
			NanTWRReports = 0;
			int l = 0, r = 0, aaddr, taddr;
			int rangeTime, valid;

			aaddr = instance_newrangeancadd() & 0xf;
#if (DISCOVERY == 1)
			taddr = instance_newrangetagadd() & 0xff;
#else
			taddr = instance_newrangetagadd() & 0xff;
#endif
			rangeTime = instance_newrangetim() & 0xffffffff;

#if (OLED == 1)

			if (printLCDTWRReports + 1500 <= portGetTickCnt())
			{
				// 每1.5S更新一次测距数据
				if (instance_mode == ANCHOR)
				{
					int b = 0;
					double rangetotag = instance_get_tagdist(toggle);

					while (((int)(rangetotag * 1000)) == 0)
					{
						if (b > (max_tag_num - 1))
							break;

						toggle++;
						if (toggle >= max_tag_num)
							toggle = 0;

						rangetotag = instance_get_tagdist(toggle);
						b++;
					}
					sprintf((char *)&lcd_data[0], "A%d-T%d: %3.2f m  ", ancaddr, toggle, rangetotag);
					LCD_DISPLAY(0, 32, lcd_data);
					toggle++;
					if (toggle >= max_tag_num)
						toggle = 0;
				}
				else if (instance_mode == TAG)
				{
					int b = 0;
					double rangetotag = instance_get_idist(toggle);

					while (((int)(rangetotag * 1000)) == 0)
					{
						if (b > (MAX_ANCHOR_LIST_SIZE - 1))
							break;

						toggle++;
						if (toggle >= MAX_ANCHOR_LIST_SIZE)
							toggle = 0;

						rangetotag = instance_get_idist(toggle);
						b++;
					}
#if (DISCOVERY == 1)
					sprintf((char *)&lcd_data[0], "T%d A%d: %3.2f m", taddr, toggle, instance_get_idist(toggle));
#else
					sprintf((char *)&lcd_data[0], "T%d-A%d: %3.2f m  ", tagaddr, toggle, instance_get_idist(toggle));
#endif
					LCD_DISPLAY(0, 32, lcd_data);
					toggle++;
					if (toggle >= MAX_ANCHOR_LIST_SIZE)
						toggle = 0;
				}
				printLCDTWRReports = portGetTickCnt();
			}

#endif
			// 发送串口数据，数据格式和协议参考使用手册
			l = instance_get_lcount() & 0xFFFF;
			if (instance_mode == TAG)
			{
				r = instance_get_rnum();
			}
			else
			{
				r = instance_get_rnuma(taddr);
			}

			valid = instance_validranges();
			UART_TX_DATA_len = 0;
			UART_TX_DATA_len2 = 0;
			if (rx == TOF_REPORT_T2A)
			{
				int A0_DIST = instance_get_idist_mm(0);
				int A1_DIST = instance_get_idist_mm(1);
				int A2_DIST = instance_get_idist_mm(2);
				int A3_DIST = instance_get_idist_mm(3);
				if (s1switch & SWS1_KAM_MODE) // 第8拨码开关=on,开启卡尔曼滤波
				{
					// 初次开机后，先获取10个测距值后，再进行卡尔曼滤波
					// 否则初值为0，测距值如果是20，会有从0到20的过程
					if (A0_DIST > 0)
					{
						if (A0_count > 20)
						{
							A0_DIST = kalman_filter(A0_DIST, 0, taddr);
						}
						else
						{
							kalman_filter(A0_DIST, 0, taddr);
						}
					}

					if (A1_DIST > 0)
					{
						if (A1_count > 20)
						{
							A1_DIST = kalman_filter(A1_DIST, 1, taddr);
						}
						else
						{
							kalman_filter(A1_DIST, 1, taddr);
						}
					}

					if (A2_DIST > 0)
					{
						if (A2_count > 20)
						{
							A2_DIST = kalman_filter(A2_DIST, 2, taddr);
						}
						else
						{
							kalman_filter(A2_DIST, 2, taddr);
						}
					}

					if (A3_DIST > 0)
					{
						if (A3_count > 20)
						{
							A3_DIST = kalman_filter(A3_DIST, 3, taddr);
						}
						else
						{
							kalman_filter(A3_DIST, 3, taddr);
						}
					}
				}

				UART_TX_DATA_len = sprintf((char *)&UART_TX_DATA[0], "mc %02x %08x %08x %08x %08x %04x %02x %08x %c%d:%d\r\n",
										   valid, A0_DIST, A1_DIST, A2_DIST, A3_DIST, l, r, rangeTime,
										   (instance_mode == TAG) ? 't' : 'a', taddr, aaddr);

				if (instance_mode == TAG) // 标签可内置定位算法 计算坐标
				{
					int result = 0;
					vec3d anchorArray[4];
					vec3d report;
					int Range_deca[4];
					// A0
					anchorArray[0].x = 0; // anchor0.x uint:m
					anchorArray[0].y = 0; // anchor0.y uint:m
					anchorArray[0].z = 2; // anchor0.z uint:m

					// A1
					anchorArray[1].x = 0;
					anchorArray[1].y = 3;
					anchorArray[1].z = 2;

					// A2
					anchorArray[2].x = 3.3;
					anchorArray[2].y = 3;
					anchorArray[2].z = 2;

					// A3
					anchorArray[3].x = 3.3;
					anchorArray[3].y = 0;
					anchorArray[3].z = 2;

					if (valid >= 0x07) // 存在3个及以上有效距离值
					{
						Range_deca[0] = A0_DIST; // tag to A0 distance
						Range_deca[1] = A1_DIST; // tag to A1 distance
						Range_deca[2] = A2_DIST; // tag to A2 distance
						Range_deca[3] = A3_DIST; // tag to A3 distance

						if (valid == 0x0f) // 采用4基站定位
						{
							result = GetLocation(&report, 1, &anchorArray[0], &Range_deca[0]);
						}
						else // 采用3基站定位
						{
							result = GetLocation(&report, 0, &anchorArray[0], &Range_deca[0]);
						}

						if (result > 0)
						{
							sprintf(Location_char, "LO=(%.2f,%.2f,%.2f)", report.x, report.y, report.z);
						}
						else
						{
							sprintf(Location_char, "LO=[NULL,NULL,NULL]");
						}
					}
					else
					{
						sprintf(Location_char, "LO=[NULL,NULL,NULL]");
					}
				}

				char A0_dis_char[6] = {0};
				char A1_dis_char[6] = {0};
				char A2_dis_char[6] = {0};
				char A3_dis_char[6] = {0};

				if ((valid & 0x01) == 0x01)
				{
					sprintf(A0_dis_char, "%.2f", (float)(A0_DIST) / 1000.0);
					if (A0_count < 30)
					{
						A0_count++;
					}
				}
				else
				{
					sprintf(A0_dis_char, "NULL");
				}
				valid = valid >> 1;

				if ((valid & 0x01) == 0x01)
				{
					sprintf(A1_dis_char, "%.2f", (float)(A1_DIST) / 1000.0);
					if (A1_count < 30)
					{
						A1_count++;
					}
				}
				else
				{
					sprintf(A1_dis_char, "NULL");
				}
				valid = valid >> 1;

				if ((valid & 0x01) == 0x01)
				{
					sprintf(A2_dis_char, "%.2f", (float)(A2_DIST) / 1000.0);
					if (A2_count < 30)
					{
						A2_count++;
					}
				}
				else
				{
					sprintf(A2_dis_char, "NULL");
				}
				valid = valid >> 1;

				if ((valid & 0x01) == 0x01)
				{
					sprintf(A3_dis_char, "%.2f", (float)(A3_DIST) / 1000.0);
					if (A3_count < 30)
					{
						A3_count++;
					}
				}
				else
				{
					sprintf(A3_dis_char, "NULL");
				}

				if (instance_mode == TAG) // 标签输出十进制测距数据和位置坐标
				{
					UART_TX_DATA_len2 = sprintf((char *)&UART_TX_DATA2[0], "$%sT%d,%s,%s,%s,%s,%s\r\n",
												(s1switch & SWS1_KAM_MODE) ? "K" : "NK", taddr, A0_dis_char, A1_dis_char, A2_dis_char, A3_dis_char, Location_char);

					strcat((char *)&UART_TX_DATA[0], (char *)&UART_TX_DATA2[0]);
					UART_TX_DATA_len = UART_TX_DATA_len + UART_TX_DATA_len2;
				}
			}
			instance_cleardisttableall();
		}
		else // 无测距数据1HZ显示null
		{
			if (instance_mode == TAG)
			{
				NanTWRReports++;
				if (NanTWRReports >= 200000)
				{
					NanTWRReports = 0;
					int taddr = instance_newrangetagadd() & 0xf;
					UART_TX_DATA_len = sprintf((char *)&UART_TX_DATA[0], "$%sT%d,NULL,NULL,NULL,NULL\r\n", (s1switch & SWS1_KAM_MODE) ? "K" : "NK", taddr);
				}
			}
		}

		if (UART_TX_DATA_len > 0)
		{
			HAL_UART_Transmit_DMA(&huart2, &UART_TX_DATA[0], UART_TX_DATA_len);
		}
	}
	return 0;
}

/*
	串口数据接收，用于接收串行指令等数据
	数据为$起始，#结束
	如$time,3212334#
*/

uint8_t UART_RX_DATA[100] = {0};
uint8_t UART_RX_DATA_len = 0;
uint8_t UART_RX_start_flag = 0;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (UART_RX_BUF[0] == '$')
	{
		UART_RX_DATA_len = 0;
		UART_RX_DATA[UART_RX_DATA_len] = UART_RX_BUF[0];
		UART_RX_start_flag = 1;
		UART_RX_DATA_len++;
	}
	else if (UART_RX_start_flag == 1)
	{
		UART_RX_DATA[UART_RX_DATA_len] = UART_RX_BUF[0];
		UART_RX_DATA_len++;
		if (UART_RX_BUF[0] == '#')
		{
			led_toggle(LED_PC7);
			UART_RX_start_flag = 0;
			HAL_UART_Transmit_DMA(&huart2, &UART_RX_DATA[0], UART_RX_DATA_len);
			/*
				数据解析和指令执行
			*/
			UART_RX_DATA_len = 0;
			// HAL_NVIC_SystemReset();//重启
		}
	}
	HAL_UART_Receive_DMA(&huart2, &UART_RX_BUF[0], 1); // 启动DMA接收
}
