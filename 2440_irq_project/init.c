
#include "s3c24xx.h"

#define TXD0READY   (1<<2)

int isBootFromNorFlash(void)
{
    volatile int *p = (volatile int *)0;
    int val;

    val = *p;              //备份原来0地址的值；
    *p = 0x12345678;       //向0地址写值；
    if (*p == 0x12345678)  //判断是否写入成功；
    {
        *p = val;          //写成功, 是nand启动，恢复原来的值；
        return 0;
    }
    else
    {
         return 1;         //NOR不能像内存一样写；
    }
}


void copy_code_to_sdram(unsigned char *src, unsigned char *dest, unsigned int len)
{    
    int i = 0;
  
    if (isBootFromNorFlash())    //如果是NOR启动；
    {
        while (i < len)          //将len长度的数据，从源地址复制到目标地址上去；
        {
            dest[i] = src[i];
            i++;
        }
    }
    else
    {
         nand_read((unsigned int)src, dest, len); //nand_read较麻烦，再后面介绍；
    }
}


void clear_bss(void)
{
    extern int __bss_start, __bss_end;  //声明链接脚本的链接地址;
    int *p = &__bss_start;            
    
    for (; p < &__bss_end; p++)         //以_bss_start开始，清理到_bss_end;
        *p = 0;
}


void nand_init(void)
{
#define TACLS   0
#define TWRPH0  1
#define TWRPH1  0
	/* 设置时序 P225*/
	NFCONF = (TACLS<<12)|(TWRPH0<<8)|(TWRPH1<<4);
	/* 使能NAND Flash控制器, 初始化ECC, 禁止片选 P226 */
	NFCONT = (1<<4)|(1<<1)|(1<<0);	
}

void nand_select(void)
{
	NFCONT &= ~(1<<1);	
}

void nand_deselect(void)
{
	NFCONT |= (1<<1);	
}

void nand_cmd(unsigned char cmd)
{
	volatile int i;
	NFCMMD = cmd;
	for (i = 0; i < 10; i++);
}

void nand_addr(unsigned int addr)
{
    unsigned int col = addr % 2048;     //对2k取余，得到列；
    unsigned int page = addr / 2048; 	//对2k取整，得到行；
    volatile int i;

    NFADDR = col & 0xff; 			    //列的7-0位；
    for (i = 0; i < 10; i++);
    NFADDR = (col >> 8) & 0xff;         //列的15-8位；
    for (i = 0; i < 10; i++);
    
    NFADDR = page & 0xff;			    //行的7-0位；
    for (i = 0; i < 10; i++);
    NFADDR = (page >> 8) & 0xff;        //行的15-8位；
    for (i = 0; i < 10; i++);
    NFADDR = (page >> 16) & 0xff;       //行的23-16位；
    for (i = 0; i < 10; i++);    
}


void nand_wait_ready(void)
{
	while (!(NFSTAT & 1));
}

unsigned char nand_data(void)
{
	return NFDATA;
}

void nand_read(unsigned int addr, unsigned char *buf, unsigned int len)
{
	int col = addr % 2048;
	int i = 0;
		
	/* 1. 选中 */
	nand_select();

	while (i < len)
	{
		/* 2. 发出读命令00h */
		nand_cmd(0x00);

		/* 3. 发出地址(分5步发出) */
		nand_addr(addr);

		/* 4. 发出读命令30h */
		nand_cmd(0x30);

		/* 5. 判断状态 */
		nand_wait_ready();

		/* 6. 读数据 */
		for (; (col < 2048) && (i < len); col++)
		{
			buf[i] = nand_data();
			i++;
			addr++;
		}
		
		col = 0;
	}

	/* 7. 取消选中 */		
	nand_deselect();
}

#define PCLK            50000000    // init.c中的clock_init函数设置PCLK为50MHz
#define UART_CLK        PCLK        //  UART0的时钟源设为PCLK
#define UART_BAUD_RATE  115200      // 波特率
#define UART_BRD        ((UART_CLK  / (UART_BAUD_RATE * 16)) - 1)

/*
 * 初始化UART0
 * 115200,8N1,无流控
 */
void uart0_init(void)
{
    GPHCON  |= 0xa0;    // GPH2,GPH3用作TXD0,RXD0
    GPHUP   = 0x0c;     // GPH2,GPH3内部上拉

    ULCON0  = 0x03;     // 8N1(8个数据位，无较验，1个停止位)
    UCON0   = 0x05;     // 查询方式，UART时钟源为PCLK
    UFCON0  = 0x00;     // 不使用FIFO
    UMCON0  = 0x00;     // 不使用流控
    UBRDIV0 = UART_BRD; // 波特率为115200
}

/*
 * 发送一个字符
 */
void putc(unsigned char c)
{
    /* 等待，直到发送缓冲区中的数据已经全部发送出去 */
    while (!(UTRSTAT0 & TXD0READY));
    
    /* 向UTXH0寄存器中写入数据，UART即自动将它发送出去 */
    UTXH0 = c;
}

void puts(char *str)
{
	int i = 0;
	while (str[i])
	{
		putc(str[i]);
		i++;
	}
}

void puthex(unsigned int val)
{
	/* 0x1234abcd */
	int i;
	int j;
	
	puts("0x");

	for (i = 0; i < 8; i++)
	{
		j = (val >> ((7-i)*4)) & 0xf;
		if ((j >= 0) && (j <= 9))
			putc('0' + j);
		else
			putc('A' + j - 0xa);
		
	}
	
}


/*
 * LED1,LED2,LED4对应GPF4、GPF5、GPF6
 */
#define	GPF4_out	(1<<(4*2))
#define	GPF5_out	(1<<(5*2))
#define	GPF6_out	(1<<(6*2))

#define	GPF4_msk	(3<<(4*2))
#define	GPF5_msk	(3<<(5*2))
#define	GPF6_msk	(3<<(6*2))

/*
 * S2,S3,S4对应GPF0、GPF2、GPG3
 */
#define GPF0_eint     (0x2<<(0*2))
#define GPF2_eint     (0x2<<(2*2))
#define GPG3_eint     (0x2<<(3*2))

#define GPF0_msk    (3<<(0*2))
#define GPF2_msk    (3<<(2*2))
#define GPG3_msk    (3<<(3*2))

void init_led(void)
{
    // LED1,LED2,LED4对应的3根引脚设为输出
    GPFCON &= ~(GPF4_msk | GPF5_msk | GPF6_msk);
    GPFCON |= GPF4_out | GPF5_out | GPF6_out;
}

/*
 * 初始化GPIO引脚为外部中断
 * GPIO引脚用作外部中断时，默认为低电平触发、IRQ方式(不用设置INTMOD)
 */ 
void init_irq(void)
{
    // S2,S3对应的2根引脚设为中断引脚 EINT0,ENT2
    GPFCON &= ~(GPF0_msk | GPF2_msk);
    GPFCON |= GPF0_eint | GPF2_eint;

    // S4对应的引脚设为中断引脚EINT11
    GPGCON &= ~GPG3_msk;
    GPGCON |= GPG3_eint;
    
    // 对于EINT11，需要在EINTMASK寄存器中使能它
    EINTMASK &= ~(1<<11);
        
    /*
     * 设定优先级：
     * ARB_SEL0 = 00b, ARB_MODE0 = 0: REQ1 > REQ3，即EINT0 > EINT2
     * 仲裁器1、6无需设置
     * 最终：
     * EINT0 > EINT2 > EINT11即K2 > K3 > K4
     */
    PRIORITY = (PRIORITY & ((~0x01) | (0x3<<7))) | (0x0 << 7) ;

    // EINT0、EINT2、EINT8_23使能
    INTMSK   &= (~(1<<0)) & (~(1<<2)) & (~(1<<5));
}

int strlen(char *str)
{
	int i = 0;
	while (str[i])
	{
		i++;
	}
	return i;
}


void strcpy(char *dest, char *src)
{
	while ((*dest++ = *src++) != '\0');
}

