#include "ctiic.h"
#include "main.h"

// 控制I2C速度的延时
void IIC_Delay(void)
{
	int t = 0;
	// 72MHz:15  I2C频率约700KHz
	// 32MHz：2  I2C频率约400KHz
	while (t < 16)
	{
		__NOP();
		t++;
	}
}
// 电容触摸芯片IIC接口初始化
void IIC_Init(void)
{
	IIC_Stop();
}
// 产生IIC起始信号
void IIC_Start(void)
{
	IIC_SDA_OUT();
	IIC_SDA(1);
	IIC_SCL(1);
	// IIC_Delay();
	IIC_SDA(0); // START:when CLK is high,DATA change form high to low
	// IIC_Delay();
	IIC_SCL(0); // 钳住I2C总线，准备发送或接收数据
}
// 产生IIC停止信号
void IIC_Stop(void)
{
	IIC_SDA_OUT();
	IIC_SDA(0);
	// IIC_Delay();
	IIC_SCL(1);
	IIC_Delay();
	IIC_SDA(1); // STOP:when CLK is high DATA change form low to high
}
// 等待应答信号到来
// 返回值：1，接收应答失败
//         0，接收应答成功
u8 IIC_Wait_Ack(void)
{
	u8 ucErrTime = 0;
	u8 rack = 0;
	IIC_SDA_IN();
	IIC_SDA(1);
	IIC_Delay();
	IIC_SCL(1);
	IIC_Delay();

	while (IIC_READ_SDA)
	{
		ucErrTime++;

		if (ucErrTime > 250)
		{
			IIC_Stop();
			rack = 1;
			break;
		}
	}

	IIC_SCL(0); // 时钟输出0
	IIC_Delay();
	return rack;
}
// 产生ACK应答
void IIC_Ack(void)
{
	IIC_SDA_OUT();
	IIC_SDA(0);
	IIC_Delay();
	IIC_SCL(1);
	IIC_Delay();
	IIC_SCL(0);
}
// 不产生ACK应答
void IIC_NAck(void)
{
	IIC_SDA_OUT();
	IIC_SDA(1);
	IIC_Delay();
	IIC_SCL(1);
	IIC_Delay();
	IIC_SCL(0);
}
// IIC发送一个字节
// 返回从机有无应答
// 1，有应答
// 0，无应答
u8 IIC_SendByte(u8 txd)
{
	u8 t, ack;
	IIC_SDA_OUT();
	// IIC_SCL(0);
	for (t = 0; t < 8; t++)
	{
		if (txd & 0x80)
			IIC_SDA(1);
		else
			IIC_SDA(0);
		// IIC_Delay();
		IIC_SCL(1);
		IIC_Delay();
		IIC_Delay();
		IIC_SCL(0);
		txd <<= 1;
	}
	IIC_SDA(1);
	IIC_SDA_IN();
	// IIC_Delay();
	IIC_SCL(1);
	IIC_Delay();
	if (IIC_READ_SDA)
		ack = 1;
	else
		ack = 0;
	IIC_SCL(0);
	IIC_SDA_OUT();
	return ack;
}
// 读1个字节，ack=1时，发送ACK，ack=0，发送nACK
u8 IIC_ReadByte(u8 ack)
{
	u8 i, receive = 0;
	IIC_SDA_IN();
	IIC_SDA(1);
	for (i = 0; i < 8; i++)
	{
		IIC_SCL(0);
		IIC_Delay();
		IIC_SCL(1);
		IIC_Delay();
		receive <<= 1;

		if (IIC_READ_SDA)
			receive |= 0x01;
		else
			receive |= 0x00;
		IIC_SCL(0);
		IIC_Delay();
	}
	// IIC_Delay();
	if (ack == 0)
		IIC_NAck(); // 发送nACK
	else
		IIC_Ack(); // 发送ACK
	return receive;
}