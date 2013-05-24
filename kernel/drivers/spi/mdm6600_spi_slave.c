/* 
 * ifx_n721_spi.c -- Serial peheripheral interface framing layer for IFX modem.
 *
 * Copyright (C) 2009 Texas Instruments
 * Authors:	Umesh Bysani <bysani@ti.com> and
 *		Shreekanth D.H <sdh@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <linux/irq.h>
#include <mach/gpio.h>
#include <plat/omap-pm.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/spi/mdm6600_spi.h>
#define WAKE_LOCK_RESUME
#ifdef WAKE_LOCK_RESUME
#include <linux/wakelock.h>
#endif
#include <linux/delay.h> 
#include <linux/earlysuspend.h>
#include <linux/spinlock.h>

#define EBS_TEST_FS
#define EBS_TEST
// EBS
#ifdef EBS_TEST_FS
#include <linux/proc_fs.h>
#include <linux/uaccess.h>	// copy_from/to_user()
#endif 
// EBS 


#define CONFIG_SPI_DEBUG
//#define CONFIG_EARLY_SUSPEND_TEST
//                                                                              

#define SPI_DEBUG_PRINT(format, args...)  printk(format , ## args)
//#define SPI_DEBUG_PRINT(format, args...) do{}while(0)

#define LOCK_T          spinlock_t
#define CREATELOCK(_l)  spin_lock_init(&(_l))
#define DELETELOCK(_l)
#define LOCK(_l)        spin_lock_bh(&(_l))
#define WAIT_UNLOCK(_l)        spin_unlock_wait(&(_l))
#define UNLOCK(_l)      spin_unlock_bh(&(_l))
#define ATOMIC(_l,_f)   spin_lock_irqsave(&(_l),(_f))
#define UNATOMIC(_l,_f) spin_unlock_irqrestore(&(_l),(_f))

static LOCK_T spi_suspend_lock;

static int is_suspended = 0;	// for spin lock

typedef struct {
	__u8 ea:1;
	__u8 cr:1;
	__u8 d:1;
	__u8 server_chn:5;
} __attribute__ ((packed)) address_field;

typedef struct {
	__u8 ea:1;
	__u8 len:7;
} __attribute__ ((packed)) short_length;

typedef struct {
	__u8 ea:1;
	__u8 l_len:7;
	__u8 h_len;
} __attribute__ ((packed)) long_length;

typedef struct {
	address_field addr;
	__u8 control;
	short_length length;
} __attribute__ ((packed)) short_frame_head;

typedef struct {
	short_frame_head h;
	__u8 data[0];
} __attribute__ ((packed)) short_frame;

typedef struct {
	address_field addr;
	__u8 control;
	long_length length;
	__u8 data[0];
} __attribute__ ((packed)) long_frame_head;

typedef struct {
	long_frame_head h;
	__u8 data[0];
} __attribute__ ((packed)) long_frame;

#define GET_LONG_LENGTH(a) ( ((a).h_len << 7) | ((a).l_len) )

void dump_atcmd(char *data) 
{

	short_frame *short_pkt = (short_frame *) data;
	long_frame *long_pkt;
	u8 *uih_data_start;
	u32 uih_len;
	int i;

	if ((short_pkt->h.length.ea) == 0) {
		long_pkt = (long_frame *) data;
		uih_len = GET_LONG_LENGTH(long_pkt->h.length);
		uih_data_start = long_pkt->h.data;
		//printk("long packet length %d\n", uih_len);
	} else {
		uih_len = short_pkt->h.length.len;
		uih_data_start = short_pkt->data;
		//printk("long packet length %d\n", uih_len);
	}
	if(uih_len>10)
		uih_len = 10;
	
	for(i=0; i<uih_len; i++)
	{
		if(uih_data_start[i]>=32 && uih_data_start[i]<=126)
			SPI_DEBUG_PRINT("%c ", uih_data_start[i]);
		else
			SPI_DEBUG_PRINT("%x ", uih_data_start[i]);
	}
	SPI_DEBUG_PRINT("\n");
}

/*                                                                            */
/* Simple Dump*/
void dump_spi_simple(char *data) 
{
  u8 *data_start;
  int i;

  data_start = data;
  
	for(i=0; i<16; i++)
	{
			SPI_DEBUG_PRINT("%x ", data_start[i]);
	}
	SPI_DEBUG_PRINT("\n");  
}
/*---------------------------------------------------------------------------*/




#define OMAP_MODEM_WAKE 121 // P2_CDMA REV.B
//                                                           
/* ################################################################################################################ */

/* Structure used to store private data */
struct ifx_spi_data {
	dev_t			devt;
	spinlock_t		spi_lock;
	struct spi_device	*spi;
	struct list_head	device_entry;
        struct completion       ifx_read_write_completion;
        struct tty_struct       *ifx_tty;

	/* buffer is NULL unless this device is open (users > 0) */
//	struct mutex		buf_lock;
	unsigned int		users;
//	unsigned int		throttle;
	struct work_struct      ifx_work;
	struct workqueue_struct *ifx_wq;
	unsigned char wq_name[20];

	unsigned int		ifx_master_initiated_transfer;
	unsigned int		ifx_spi_count;
	unsigned int		ifx_sender_buf_size;
	unsigned int		ifx_receiver_buf_size;
	unsigned int		ifx_current_frame_size;
	unsigned int		ifx_valid_frame_size;
	unsigned int		ifx_ret_count;
	const unsigned char 	*ifx_spi_buf;
	unsigned char		*ifx_tx_buffer;
	unsigned char           *ifx_rx_buffer;
	unsigned		mrdy_gpio;
	unsigned		srdy_gpio;
	unsigned int wcount;
#ifdef CONFIG_EARLY_SUSPEND_TEST
	struct early_suspend early_suspend;
#endif
#ifdef WAKE_LOCK_RESUME
	struct wake_lock wake_lock;
#endif
	unsigned int		wake_lock_flag;
	unsigned int		is_suspended;	// EBS
};

union ifx_spi_frame_header{
	struct{
		unsigned int curr_data_size:12;
		unsigned int more:1;
		unsigned int res1:1;
		unsigned int res2:2;      
		unsigned int next_data_size:12;
		unsigned int ri:1;
		unsigned int dcd:1;
		unsigned int cts_rts:1;
		unsigned int dsr_dtr:1;
	}ifx_spi_header;
	unsigned char framesbytes[IFX_SPI_HEADER_SIZE];
};

/*spi_data_table is consist of 1 spi_data for each mcspi port */
struct ifx_spi_data* spi_data_table[IFX_N_SPI_MINORS];
struct tty_driver 	*ifx_spi_tty_driver;


#ifdef CONFIG_EARLY_SUSPEND_TEST
static void ifx_spi_early_suspend(struct early_suspend *h);
static void ifx_spi_late_resume(struct early_suspend *h);
#endif

/* ################################################################################################################ */
/* Global Declarations */
/* Function Declarations */
static void ifx_spi_set_header_info(unsigned char *header_buffer, unsigned int curr_buf_size, unsigned int next_buf_size);
static int ifx_spi_get_header_info(unsigned char *rx_buffer, unsigned int *valid_buf_size);
static void ifx_spi_set_mrdy_signal(struct ifx_spi_data *spi_data, int value);
static void ifx_spi_setup_transmission(struct ifx_spi_data *spi_data);
static void ifx_spi_setup_transmit_and_receive(struct ifx_spi_data *spi_data); //deinsala
static void ifx_spi_setup_receive(struct ifx_spi_data *spi_data); //deinsala
static void ifx_spi_send_and_receive_data(struct ifx_spi_data *spi_data);
static void ifx_spi_receive_data(struct ifx_spi_data *spi_data); //deinsala
static int ifx_spi_get_next_frame_size(int count);
static int ifx_spi_allocate_frame_memory(struct ifx_spi_data *spi_data, unsigned int memory_size);
static void ifx_spi_free_frame_memory(struct ifx_spi_data *spi_data);
static void ifx_spi_buffer_initialization(struct ifx_spi_data *spi_data);
static unsigned int ifx_spi_sync_read_write(struct ifx_spi_data *spi_data, unsigned int len);
static unsigned int ifx_spi_sync_read(struct ifx_spi_data *spi_data, unsigned int len); //deinsala
static irqreturn_t ifx_spi_handle_srdy_irq(int irq, void *handle);
static void ifx_spi_handle_work(struct work_struct *work);

//#define ETJO_DUMP
//                                                                                     
/*---------------------------------------------------------------------------*/

static void dump_spi_buffer(const unsigned char *txt, const unsigned char *buf, int count);
/*                                                                           */
/* Smart Dump */
#ifdef ETJO_DUMP
void dump_spi_smart(const struct ifx_spi_data *spi_data)
{
	int i = 0;
	static unsigned int seq_no = 0;

	printk("\n--------------------------------------\nseqNo : %ld\nSPI TX : ", seq_no++);
	for( ; i < 16; i++)
	{
		printk("%02x ", *(((unsigned char*)(spi_data->ifx_tx_buffer)) + i));
	}
	printk("\nSPI RX : ");
	for(i = 0; i < 16; i++)
	{
		printk("%02x ", *(((unsigned char*)(spi_data->ifx_rx_buffer)) + i));
	}
    dump_spi_buffer("\nSPI[W]", ((unsigned char*)(spi_data->ifx_tx_buffer)), spi_data->ifx_spi_count);
    dump_spi_buffer("SPI[R]", ((unsigned char*)(spi_data->ifx_rx_buffer)), spi_data->ifx_spi_count);
	printk("--------------------------------------\n");

}
#endif /* ETJO_DUMP */
// hgahn
unsigned char rx_dummy[]={0xff,0xff,0xff,0xff};

static DEFINE_MUTEX(mspi_tx_rx_mutex);

/* ################################################################################################################ */

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

/* ################################################################################################################ */

#if defined(EBS_TEST_FS) //def CONFIG_PROC_FS
/*--------------------------------------------------------------------
 * BEGINS: Proc file system related functions for SPI.
 *--------------------------------------------------------------------
 */
static unsigned int log_enable_flag;

#define	RIL_SPI_PROC_FILE "driver/spi_log_enable"

static struct proc_dir_entry *ril_spi_proc_file;

static ssize_t spi_proc_read(struct file *filp, char *buf, size_t len, loff_t *offset)
{

	printk(KERN_INFO "[SPI_MDM6600] Log_enable_flag  = [%d] \n", log_enable_flag);

	return 0;
}

static ssize_t spi_proc_write(struct file *filp, const char *buf, size_t len, loff_t *off)
{
	char messages[12];
	u32 val;
	u32 reg;
	s32 err;
	char cmd;

	if(len > 12)
		len = 12;

	if(copy_from_user(messages, buf, len))
		return -EFAULT;

	sscanf(buf, "%c 0x%x 0x%x", &cmd, &reg, &val);

	printk(KERN_INFO "[SPI_MDM6600] LGE: ril_spi_proc_write \n");


	switch(cmd)
	{
		case '1':
			log_enable_flag = 1;
		//                                                                                         
			printk(KERN_INFO "[SPI_MDM6600] LGE: P2 SPI log_enable = %d\n", log_enable_flag);
			break;
		
		case '0':
				log_enable_flag = 0;
			//                                                                                         
				printk(KERN_INFO "[SPI_MDM6600] LGE: P2 SPI log_enable = %d\n", log_enable_flag);
				break;
		default :
			printk(KERN_INFO "[SPI_MDM6600] LGE: P2 SPI invalid command\n");
		    printk(KERN_INFO "[SPI_MDM6600] w: log on command !! \n");
		  //  printk(KERN_INFO "[MUIC] or \"w address value\"\n");
			break;
	}
	return len;
}

static struct file_operations ril_spi_proc_ops = 
{
	.read = spi_proc_read,
	.write = spi_proc_write,
};

static void create_ril_spi_proc_file(void)
{
	ril_spi_proc_file = create_proc_entry(RIL_SPI_PROC_FILE, 0777, NULL);

	if(ril_spi_proc_file) 
	{
		//hub_muic_proc_file->owner = THIS_MODULE; // 20100617 kernel API changed
		ril_spi_proc_file->proc_fops = &ril_spi_proc_ops;
	}
	else
		printk(KERN_INFO "[MUIC] LGE: Hub MUIC proc file create failed!\n");
}

static void remove_ril_spi_proc_file(void)
{
	remove_proc_entry(RIL_SPI_PROC_FILE, NULL);
}

/*--------------------------------------------------------------------
 * ENDS: Proc file system related functions for MUIC.
 *--------------------------------------------------------------------
 */
#endif //EBS_TEST_FS end 




/* IFX SPI Operations */

/*
 * Function opens a tty device when called from user space
 */
static int 
ifx_spi_open(struct tty_struct *tty, struct file *filp)
{
	struct ifx_spi_data *spi_data;

	switch (tty->index)
	{
		case 3: /* ttyspi3 for CP */
			spi_data = spi_data_table[tty->index];
			break;
		case 0: /* Not supported */
		case 1: /* Not supported */
		case 2: /* Not supported */
		default: /* Never reach here */
			printk("ifx_spi_open: tty->index : %d not support\n", tty->index );
			spi_data = NULL;
			return -1;
	}

	ifx_spi_buffer_initialization(spi_data);
	if (spi_data->users++ == 0)
	{
		tty->driver_data = (void *)spi_data;
		spi_data->ifx_tty = tty;
	}

	return 0;
}

/*
 * Function closes a opened a tty device when called from user space
 */
static void 
ifx_spi_close(struct tty_struct *tty, struct file *filp)
{  
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;

	if(!spi_data) return;
	if(spi_data->users == 0) return;

	if(--spi_data->users == 0)
	{
		spi_data->ifx_tty = NULL;
		tty->driver_data = NULL;
	}
}

/*
 * Function is called from user space to send data to MODEM, it setups the transmission, enable MRDY signal and
 * waits for SRDY signal HIGH from MDOEM. Then starts transmission and reception of data to and from MODEM.
 * Once data read from MODEM is transferred to TTY core flip buffers, then "ifx_read_write_completion" is set
 * and this function returns number of bytes sent to MODEM
 */

#ifdef LGE_DUMP_SPI_BIFFUER
#define COL_SIZE 16
static void dump_spi_buffer(const unsigned char *txt, const unsigned char *buf, int count)
{
    char dump_buf_str[COL_SIZE+1];

    if (buf != NULL) 
    {
        int j = 0;
        char *cur_str = dump_buf_str;
        unsigned char ch;
        while((j < COL_SIZE) && (j  < count))
        {
            ch = buf[j];
            if ((ch < 32) || (ch > 126))
            {
                *cur_str = '.';
            } else
            {
                *cur_str = ch;
            }
            cur_str++;
            j++;
        }
        *cur_str = 0;
        printk("%s:count:%d [%s]\n", txt, count, dump_buf_str);                        
    }
    else
    {
        printk("%s: buffer is NULL\n", txt);                 
    }
}
#else
#define dump_spi_buffer(...)
#endif

static int 
ifx_spi_write(struct tty_struct *tty, const unsigned char *buf, int count)
{	
	int ret;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;
	//printk("User write, v.S7\n");
	if(!spi_data) return 0;

#ifdef LGE_DUMP_SPI_BIFFUER
//	printk("\n\n[OMAP_SPI_WRITE] - AP - [START] \n");
	if(log_enable_flag == 1)
    	dump_spi_buffer("SPI[W]", buf, count);
#endif


	if( !buf ){
		printk("File: ifx_n721_spi.c\tFunction: int ifx_spi_write()\t Buffer NULL\n");
		return 0;
	}
	if(!count){
		printk("File: ifx_n721_spi.c\tFunction: int ifx_spi_write()\t Count is ZERO\n");
		return 0;
	}
	//gpio_set_value(127 ,1);
	mutex_lock(&mspi_tx_rx_mutex);
	spi_data->ifx_ret_count = 0;
	spi_data->ifx_tty = tty;
	spi_data->ifx_tty->low_latency = 1;
//	spi_data->ifx_master_initiated_transfer  = 1;
	spi_data->ifx_spi_buf  = buf;
	if (count > IFX_SPI_MAX_BUF_SIZE) {
		spi_data->ifx_spi_count = IFX_SPI_MAX_BUF_SIZE;
		printk("User write trying to write %d bytes, more than IFX_SPI_MAX_BUF_SIZE\n", count);
	}
	else {
	spi_data->ifx_spi_count  = count;
	}

/*
                                
                                                       
     
                                        
      

                                        
*/
	
	ifx_spi_buffer_initialization(spi_data);	

	//ifx_spi_setup_transmission(spi_data);
	ifx_spi_setup_transmit_and_receive(spi_data);
	//gpio_set_value(127 ,1);
	ifx_spi_send_and_receive_data(spi_data);


//	omap_pm_set_max_sdma_lat(&pm_qos_handle_for_spi,-1);
//                                                                                                                 
	//gpio_set_value(127 ,0);
	
    ret = spi_data->ifx_ret_count;
	//dump_atcmd(buf+2) ; 	
	//wait_for_completion(&spi_data->ifx_read_write_completion);
	//init_completion(&spi_data->ifx_read_write_completion);
#ifdef SPI_LOG_ENABLE_SHIM
	printk("[IFX_SPI_WRITE] - AP - [END] \n\n");
#endif	
	mutex_unlock(&mspi_tx_rx_mutex);
//	gpio_set_value(127 ,0);
	return ret; /* Number of bytes sent to the device */
}

/* This function should return number of free bytes left in the write buffer in this case always return 2048 */

static int 
ifx_spi_write_room(struct tty_struct *tty)
{
    return IFX_SPI_MAX_BUF_SIZE;	
//	return 2048;
}


/* ################################################################################################################ */
/* These two functions are to be used in future to implement flow control (RTS & CTS)*/
/*static void 
ifx_spi_throttle(struct tty_struct *tty)
{
	unsigned int flags;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;
	spi_data->ifx_tty = tty;
	spin_lock_irqsave(&spi_data->spi_lock, flags);
	spi_data->throttle = 1;
	spin_unlock_irqrestore(&spi_data->spi_lock, flags);
}

static void 
ifx_spi_unthrottle(struct tty_struct *tty)
{
	unsigned int flags;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;
	spi_data->ifx_tty = tty;

static void 
ifx_spi_unthrottle(struct tty_struct *tty)
{
	unsigned int flags;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;
	spi_data->ifx_tty = tty;
static void 
ifx_spi_unthrottle(struct tty_struct *tty)
{
	unsigned int flags;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;
	spi_data->ifx_tty = tty;
static void 
ifx_spi_unthrottle(struct tty_struct *tty)
{
	unsigned int flags;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;
	spi_data->ifx_tty = tty;
static void 
ifx_spi_unthrottle(struct tty_struct *tty)
{
	unsigned int flags;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;
	spi_data->ifx_tty = tty;
static void 
ifx_spi_unthrottle(struct tty_struct *tty)
{
	unsigned int flags;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)tty->driver_data;
	spi_data->ifx_tty = tty;
	spin_lock_irqsave(&spi_data->spi_lock, flags);
	spi_data->throttle = 0;
	if( ifx_rx_buffer != NULL ){
	     tty_insert_flip_string(spi_data->ifx_tty, ifx_rx_buffer, valid_buffer_count);
	}
	spin_unlock_irqrestore(&spi_data->spi_lock, flags);  
}*/
/* ################################################################################################################ */

/* End of IFX SPI Operations */


static void 
ifx_spi_ap_ready(struct spi_device *spi, int enable)
{
	struct ifx_spi_data *spi_data = spi_data_table[spi->master->bus_num - 1];
	if(!spi_data) {
		printk("ifx_spi_ap_ready, spi_data is null\n");
		return;
	}
	ifx_spi_set_mrdy_signal(spi_data, enable);
//	printk("spi_set_mrdy_signal set. bus_num = %d\n", spi->master->bus_num );
}

/* ################################################################################################################ */

/* TTY - SPI driver Operations */

static int ifx_spi_probe(struct spi_device *spi)
{
	int status;
	struct ifx_spi_data *spi_data;
	struct ifx_spi_platform_data *spi_pd = (struct ifx_spi_platform_data *)spi->dev.platform_data;

	/* Allocate SPI driver data */
	spi_data = (struct ifx_spi_data*)kzalloc(sizeof(struct ifx_spi_data), GFP_KERNEL);
	if (!spi_data){
		return -ENOMEM;
        }
	
	status = ifx_spi_allocate_frame_memory(spi_data, IFX_SPI_MAX_BUF_SIZE + IFX_SPI_HEADER_SIZE);
        if(status != 0){
		printk("File: ifx_n721_spi.c\tFunction: int ifx_spi_probe\tFailed to allocate memory for buffers\n");
		return -ENOMEM;
        }
	spi_data->wake_lock_flag = 0;
	
        dev_set_drvdata(&spi->dev,spi_data);
        spin_lock_init(&spi_data->spi_lock);
		CREATELOCK(spi_suspend_lock);
	//mutex_init(&mspi_tx_rx_mutex); /* uncomment for dynamically created mutex */
        INIT_WORK(&spi_data->ifx_work,ifx_spi_handle_work);
//	printk("!!!!!!!!!!!!!!!!!!!!mdm6600_probe [START] !!!!!!!!!!!!!!!!!!!!\n");
	sprintf(spi_data->wq_name, "spiwq%d", spi->master->bus_num);
//	spi_data->ifx_wq = create_rt_workqueue(spi_data->wq_name); //create_singlethread_workqueue
	spi_data->ifx_wq = create_singlethread_workqueue(spi_data->wq_name); //create_singlethread_workqueue
	if(!spi_data->ifx_wq){
		printk("Failed to setup workqueue - ifx_wq \n");          
        }
	init_completion(&spi_data->ifx_read_write_completion);
	
	spi_data->mrdy_gpio = IFX_MRDY_GPIO;
	spi_data->srdy_gpio = IFX_SRDY_GPIO;
	
	/* Configure SPI */
	spi_data->spi = spi;
	spi->mode = SPI_MODE_1;
	spi->bits_per_word = 32;
	spi->slave_ready = ifx_spi_ap_ready;
	status = spi_setup(spi);
	
	if(status < 0){
		printk("Failed to setup SPI \n");
	}             
	if (gpio_request(spi_data->srdy_gpio, "ifx srdy") < 0) {
		printk(KERN_ERR "Can't get SRDY GPIO\n");
	}
	gpio_direction_input(spi_data->srdy_gpio);

	if (gpio_request(spi_data->mrdy_gpio, "ifx mrdy") < 0) {
		printk(KERN_ERR "Can't get MRDY GPIO\n");
	}
	gpio_direction_output(spi_data->mrdy_gpio, 0);


//	printk("!!!!!!!!!!!!!!!!!!!!ifx_spi_ap_ready [gpio_direction_output after] !!!!!!!!!!!!!!!!!!!!\n");	
	//                                                                                
	if (gpio_request(OMAP_MODEM_WAKE, "ap sleep check") < 0) {
		printk(KERN_ERR "can't get synaptics pen down GPIO\n");
		return -EIO;
	}
//	printk("!!!!!!!!!!!!!!!!!!!!ifx_spi_ap_ready [OMAP_MODEM_WAKE after] !!!!!!!!!!!!!!!!!!!!\n");	
#if 0 // XXX_mbk 
	gpio_direction_output(OMAP_MODEM_WAKE, 1);
	gpio_set_value(OMAP_MODEM_WAKE,1);
#else
	gpio_direction_output(OMAP_MODEM_WAKE, 0);
	gpio_set_value(OMAP_MODEM_WAKE,0);
#endif
	//             
#ifdef WAKE_LOCK_RESUME
		wake_lock_init(&spi_data->wake_lock, WAKE_LOCK_SUSPEND, "mspi_wake");
#endif

#if 0
	if (gpio_request(127, "spi check") < 0) {
		printk(KERN_ERR "can't get synaptics pen down GPIO\n");
		return;
	}
	//                                                                                          
	gpio_direction_output(127, 0);
	gpio_set_value(127,0);
	//             
#endif
	
	spi->irq = OMAP_GPIO_IRQ(spi_data->srdy_gpio);
	/* Enable SRDY Interrupt request - If the SRDY signal is high then ifx_spi_handle_srdy_irq() is called */
	status = request_irq(spi->irq, ifx_spi_handle_srdy_irq,  IRQF_TRIGGER_RISING, spi->dev.driver->name, spi_data);
	if (status != 0){
		printk(KERN_ERR "Failed to request IRQ for SRDY\n");
		printk(KERN_ERR "IFX SPI Probe Failed\n");
		ifx_spi_free_frame_memory(spi_data);
		if(spi_data){
			kfree(spi_data);
		}          
	}
	
	enable_irq_wake(spi->irq);
	ifx_spi_buffer_initialization(spi_data);
	spi_data_table[spi->master->bus_num - 1] = spi_data;

#ifdef CONFIG_EARLY_SUSPEND_TEST
	spi_data->early_suspend.level = 40;
	spi_data->early_suspend.suspend = ifx_spi_early_suspend;
	spi_data->early_suspend.resume = ifx_spi_late_resume;
	register_early_suspend(&spi_data->early_suspend);
#endif
//                                                 
#if defined(EBS_TEST_FS)
	create_ril_spi_proc_file();
#endif 
//                                                 

	printk("OMAP2_MCSPI_probe [END] \n");
	return status;
}

static int 
ifx_spi_remove(struct spi_device *spi)
{	
	struct ifx_spi_data *spi_data;
	spi_data = spi_get_drvdata(spi);
	spin_lock_irq(&spi_data->spi_lock);
	spi_data->spi = NULL;
	spi_set_drvdata(spi, NULL);
	spin_unlock_irq(&spi_data->spi_lock);

	ifx_spi_free_frame_memory(spi_data);
	if(spi_data){
		kfree(spi_data);
	}          
	spi_data_table[spi->master->bus_num - 1] = NULL;
//                                                 
#if defined(EBS_TEST_FS)
	remove_ril_spi_proc_file();
#endif 
//                                                 
	return 0;
}

#ifdef CONFIG_EARLY_SUSPEND_TEST
static void 
ifx_spi_early_suspend(struct early_suspend *h)
{
#if 1
#ifdef SPI_LOG_ENABLE_SHIM
		printk("[LGE-SPI] ifx_spi_early_suspend modem_chk = %d \n",gpio_get_value(OMAP_MODEM_WAKE));
#endif
#if 0 // XXX_mbk 
		gpio_direction_output(OMAP_MODEM_WAKE,0);
		gpio_set_value(OMAP_MODEM_WAKE,0);
#endif
		return;
#endif
	return;
}
static void 
ifx_spi_late_resume(struct early_suspend *h)
{
#ifdef SPI_LOG_ENABLE_SHIM
	printk(KERN_ERR "[LGE-SPI] ifx_spi_late_resume modem_chk = %d \n",gpio_get_value(OMAP_MODEM_WAKE));
#endif
#if 0 //XXX_mbk 
	gpio_direction_output(OMAP_MODEM_WAKE,1);
	gpio_set_value(OMAP_MODEM_WAKE,1);
#endif
	return;
}
#endif
static int
ifx_spi_suspend(struct spi_device *spi, pm_message_t mesg)
{
//#ifdef SPI_LOG_ENABLE_SHIM
  //                                                                           
//#endif
 //   gpio_direction_output(OMAP_MODEM_WAKE,0);
  //  gpio_set_value(OMAP_MODEM_WAKE,0);
#ifdef EBS_TEST
	unsigned long flags;
	spin_lock_irqsave(&spi_suspend_lock, flags);
	is_suspended = 1;
	spin_unlock_irqrestore(&spi_suspend_lock, flags);
	printk("#####################\n");
	printk("[IFX_SPI SUSPEND] is_suspend_flag = %d\n", is_suspended);
#endif /* EBS_TEST */
    return 0;
}

static int
ifx_spi_resume(struct spi_device *spi)
{
//#ifdef SPI_LOG_ENABLE_SHIM
//                                                                            
//#endif
 //   gpio_direction_output(OMAP_MODEM_WAKE,1);
   // gpio_set_value(OMAP_MODEM_WAKE,1);
#ifdef EBS_TEST
	unsigned long flags;
	spin_lock_irqsave(&spi_suspend_lock, flags);
	is_suspended = 0;
	spin_unlock_irqrestore(&spi_suspend_lock, flags);
	printk("[IFX_SPI RESUME] is_suspend_flag = %d\n", is_suspended);
#endif /* EBS_TEST */    
    return 0;
}
/* End of TTY - SPI driver Operations */

/* ################################################################################################################ */

static struct spi_driver ifx_spi_driver = {
	.driver = {
		.name = "ifxn721",
                .bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
	.probe = ifx_spi_probe,
	.remove = __devexit_p(ifx_spi_remove),
    .suspend = ifx_spi_suspend,
    .resume = ifx_spi_resume,
};

/*
 * Structure to specify tty core about tty driver operations supported in TTY SPI driver.
 */
static const struct tty_operations ifx_spi_ops = {
    .open = ifx_spi_open,
    .close = ifx_spi_close,
    .write = ifx_spi_write,
    .write_room = ifx_spi_write_room,
    //.throttle = ifx_spi_throttle,
    //.unthrottle = ifx_spi_unthrottle,
    //.set_termios = ifx_spi_set_termios,
};

/* ################################################################################################################ */

/*
 * Intialize frame sizes as "IFX_SPI_DEFAULT_BUF_SIZE"(128) bytes for first SPI frame transfer
 */
static void 
ifx_spi_buffer_initialization(struct ifx_spi_data *spi_data)
{
	spi_data->ifx_sender_buf_size = IFX_SPI_DEFAULT_BUF_SIZE;
	spi_data->ifx_receiver_buf_size = IFX_SPI_DEFAULT_BUF_SIZE;
}

/*
 * Allocate memeory for TX_BUFFER and RX_BUFFER
 */
static int 
ifx_spi_allocate_frame_memory(struct ifx_spi_data *spi_data, unsigned int memory_size)
{
	int status = 0;

	spi_data->ifx_tx_buffer = kmalloc(memory_size, GFP_KERNEL);
	if (!spi_data->ifx_tx_buffer)
	{
			printk("Open Failed ENOMEM\n");
			status = -ENOMEM;
		}
	spi_data->ifx_rx_buffer = kmalloc(memory_size, GFP_KERNEL);


	if (!spi_data->ifx_rx_buffer){
		printk("Open Failed ENOMEM\n");
		status = -ENOMEM;
	}

	if(status == -ENOMEM){
		if(spi_data->ifx_tx_buffer)
		{
			kfree(spi_data->ifx_tx_buffer);
		}
		if(spi_data->ifx_rx_buffer){
			kfree(spi_data->ifx_rx_buffer);            
		}
	}
	return status;
}
static void 
ifx_spi_free_frame_memory(struct ifx_spi_data *spi_data)
{
	if(spi_data->ifx_tx_buffer)
	{
		kfree(spi_data->ifx_tx_buffer);
	}
	if(spi_data->ifx_rx_buffer){
		kfree(spi_data->ifx_rx_buffer);            
	}
}

/*
 * Function to set header information according to IFX SPI framing protocol specification
 */
static void 
ifx_spi_set_header_info(unsigned char *header_buffer, unsigned int curr_buf_size, unsigned int next_buf_size)
{
	int i;
	union ifx_spi_frame_header header;
	for(i=0; i<4; i++){
		header.framesbytes[i] = 0;
	}

	header.ifx_spi_header.curr_data_size = curr_buf_size;
	if(next_buf_size){
		header.ifx_spi_header.more=1;
		header.ifx_spi_header.next_data_size = next_buf_size;
	}
	else{
		header.ifx_spi_header.more=0;
		header.ifx_spi_header.next_data_size = 128;
	}

	for(i=3; i>=0; i--){
	header_buffer[i] = header.framesbytes[/*3-*/i];
	}
}

/*
 * Function to get header information according to IFX SPI framing protocol specification
 */
static int 
ifx_spi_get_header_info(unsigned char *rx_buffer, unsigned int *valid_buf_size)
{
	int i;
	union ifx_spi_frame_header header;

	for(i=0; i<4; i++){
		header.framesbytes[i] = 0;
	}

	for(i=3; i>=0; i--){
		header.framesbytes[i] = rx_buffer[/*3-*/i];
	}

	*valid_buf_size = header.ifx_spi_header.curr_data_size;
	if(header.ifx_spi_header.more)
	{
		printk(KERN_ERR "ifx_spi_get_header_info, There is more packet = %d\n", header.ifx_spi_header.next_data_size);
		return header.ifx_spi_header.next_data_size;
	}
	return 0;
}

/*
 * Function to set/reset MRDY signal
 */
static void 
ifx_spi_set_mrdy_signal(struct ifx_spi_data *spi_data, int value)
{
#ifdef SPI_LOG_ENABLE_SHIM
	printk("[LGE-SPI] SET MRDY SIGNAL, %d\n", value);
#endif
	gpio_set_value(spi_data->mrdy_gpio, value);

}

/*
 * Function to calculate next_frame_size required for filling in SPI frame Header
 */
static int 
ifx_spi_get_next_frame_size(int count)
{
	if(count > IFX_SPI_MAX_BUF_SIZE){
		return IFX_SPI_MAX_BUF_SIZE;    
	}
	else{   
		return count;
	}
}

/*
 * Function to setup transmission and reception. It implements a logic to find out the ifx_current_frame_size,
 * valid_frame_size and sender_next_frame_size to set in SPI header frame. Copys the data to be transferred from 
 * user space to TX buffer and set MRDY signal to HIGH to indicate Master is ready to transfer data.
 */
static void 
ifx_spi_setup_transmission(struct ifx_spi_data *spi_data)
{
	if( (spi_data->ifx_sender_buf_size != 0) || (spi_data->ifx_receiver_buf_size != 0) ){
		if(spi_data->ifx_sender_buf_size > spi_data->ifx_receiver_buf_size){
			spi_data->ifx_current_frame_size = spi_data->ifx_sender_buf_size;
		}
		else{ 
			spi_data->ifx_current_frame_size = spi_data->ifx_receiver_buf_size;    
		}
		if(spi_data->ifx_spi_count > 0){
			if(spi_data->ifx_spi_count > spi_data->ifx_current_frame_size){
				spi_data->ifx_valid_frame_size = spi_data->ifx_current_frame_size;
				spi_data->ifx_spi_count -= spi_data->ifx_current_frame_size;
			}
			else{
				spi_data->ifx_valid_frame_size = spi_data->ifx_spi_count;
				spi_data->ifx_spi_count = 0;
			}
                }
		else{
			spi_data->ifx_valid_frame_size = 0;
			spi_data->ifx_sender_buf_size = 0;
		}
		spi_data->ifx_sender_buf_size = ifx_spi_get_next_frame_size(spi_data->ifx_spi_count);

		/* memset buffers to 0 */
		memset(spi_data->ifx_tx_buffer,0,IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE);
		memset(spi_data->ifx_rx_buffer,0,IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE);

		/* Set header information */
		ifx_spi_set_header_info(spi_data->ifx_tx_buffer, spi_data->ifx_valid_frame_size, spi_data->ifx_sender_buf_size);
		if( spi_data->ifx_valid_frame_size > 0 ){      
			memcpy(spi_data->ifx_tx_buffer+IFX_SPI_HEADER_SIZE, spi_data->ifx_spi_buf, spi_data->ifx_valid_frame_size);
			spi_data->ifx_spi_buf += spi_data->ifx_valid_frame_size;
		}
	}
}

static void 
ifx_spi_setup_transmit_and_receive(struct ifx_spi_data *spi_data)
{

	/* memset buffers to 0 */

	#if 0 //defined(shim_test)
		memset(spi_data->ifx_tx_buffer,0,2048 + 64);
		memset(spi_data->ifx_rx_buffer,0,2048 + 64);
	
	#else
		memset(spi_data->ifx_tx_buffer,0,IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE);
		memset(spi_data->ifx_rx_buffer,0,IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE);
	#endif
	
	
	ifx_spi_set_header_info(spi_data->ifx_tx_buffer, spi_data->ifx_spi_count, 0);

	memcpy(spi_data->ifx_tx_buffer+IFX_SPI_HEADER_SIZE, spi_data->ifx_spi_buf, spi_data->ifx_spi_count);
	//spi_data->ifx_spi_buf = NULL;

}

static void 
ifx_spi_setup_receive(struct ifx_spi_data *spi_data)
{
	/* memset buffers to 0 */
	memset(spi_data->ifx_tx_buffer,0,IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE);
	memset(spi_data->ifx_rx_buffer,0,IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE);
}



static void
ifx_spi_tty_callback( struct ifx_spi_data *spi_data)
{

	unsigned int rx_valid_buf_size;
	/* Handling Received data */
#ifdef SPI_TEST
#else
	spi_data->ifx_receiver_buf_size = ifx_spi_get_header_info(spi_data->ifx_rx_buffer, &rx_valid_buf_size);
#endif

//	if((spi_data->throttle == 0) && (rx_valid_buf_size != 0) && !(spi_data->ifx_spi_lock)){
#ifdef SPI_TEST
#else
	if((spi_data->users > 0) && (rx_valid_buf_size != 0))
#endif
	{
#ifdef LGE_DUMP_SPI_BIFFUER
		if(log_enable_flag == 1)
			dump_spi_buffer("SPI[R]", &(spi_data->ifx_rx_buffer[4]), rx_valid_buf_size);
#endif

#ifdef SPI_TEST
		tty_insert_flip_string(spi_data->ifx_tty, spi_data->ifx_rx_buffer, SPI_MAX_PACKET_LEN);
		tty_flip_buffer_push(spi_data->ifx_tty);
#else
#ifdef SPI_LOG_ENABLE_SHIM
		printk("[IFX_SPI_WRITE] [tty data push ] %d %d %d\n",rx_valid_buf_size,spi_data->ifx_receiver_buf_size,omap_readl(0x48056078));
#endif


#ifdef SPI_LOG_ENABLE_SHIM
//		SPI_DEBUG_PRINT("SPI RX : ");
/*                                                                            */
/* Simple Dump*/
//		dump_atcmd(spi_data->ifx_rx_buffer+IFX_SPI_HEADER_SIZE+2) ;
 //   dump_spi_simple(spi_data->ifx_rx_buffer);
/*---------------------------------------------------------------------------*/
//		printk("\n");
#endif
		if(rx_valid_buf_size > SPI_MAX_PACKET_LEN) // validity check 
			return ;
		
		tty_insert_flip_string(spi_data->ifx_tty, spi_data->ifx_rx_buffer+IFX_SPI_HEADER_SIZE, rx_valid_buf_size);
		tty_flip_buffer_push(spi_data->ifx_tty);
#endif
	}  
}

/*
 * Function starts Read and write operation and transfers received data to TTY core. It pulls down MRDY signal
 * in case of single frame transfer then sets "ifx_read_write_completion" to indicate transfer complete.
 */
static void 
ifx_spi_send_and_receive_data(struct ifx_spi_data *spi_data)
{
	int status = 0; 
#ifdef SPI_LOG_ENABLE_SHIM
//	printk("[OMAP_SPI_WRITE] SPI READ & WRITE -----------------[START] \n");
	
	//                                                                                              
#endif
	                   
	status = ifx_spi_sync_read_write(spi_data, IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE); /* 4 bytes for header */                       

#ifdef SPI_LOG_ENABLE_SHIM

	//                                                                                              
//	printk("[OMAP_SPI_WRITE] SPI READ & WRITE -----------------[END] \n");
//	printk("seqNo : %lu. \n", seq_no++);
//	SPI_DEBUG_PRINT("SPI TX : ");
/*                                                                            */
/* Simple Dump*/
//	dump_atcmd(spi_data->ifx_tx_buffer+IFX_SPI_HEADER_SIZE+2) ;
// 	dump_spi_simple(spi_data->ifx_tx_buffer);
/*---------------------------------------------------------------------------*/
//	printk("\n");
//	SPI_DEBUG_PRINT("SPI RX : ");
//    dump_spi_simple(spi_data->ifx_rx_buffer);
//	printk("\nTRANSFER DONE. %lu\n", seq_no );

#endif

#ifdef SPI_TEST
	{
		int i;
		for(i=0;i< SPI_MAX_PACKET_LEN; i++)
		{
//			if(spi_data->ifx_rx_buffer[i] != (i%256)){
			if(spi_data->ifx_rx_buffer[i] != 0xAC){
				printk(KERN_EMERG "rxbuf data error:[%d] %d\n", i, spi_data->ifx_rx_buffer[i]);
				break;
			}
		}
	}
#endif

	if(status > 0) //Status is not clear, I think in case of something TX this should be m.actual_length
	{
		memset(spi_data->ifx_tx_buffer,0,IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE);
		//spi_data->ifx_ret_count += spi_data->ifx_valid_frame_size;
		// deinsala if Ok just set the ifx_ret_count to what I wrote (i.e.ifx_spi_count)
		spi_data->ifx_ret_count = spi_data->ifx_spi_count;
	}
	else
	{
		printk("[EBS-SPI] TX SPI Failure !! STATUS = %d\n", status);
		return ;
		
	}
#if 0
	if(*((int*)spi_data->ifx_rx_buffer) == 0xFFFFFFFF)
	{
		spi_data->ifx_receiver_buf_size = 0;
		printk("Recv is Nothing~~!! By Shim\n");
		return;
	}
#endif
	ifx_spi_tty_callback(spi_data);
}

/*
 * Function starts Read and write operation and transfers received data to TTY core. It pulls down MRDY signal
 * in case of single frame transfer then sets "ifx_read_write_completion" to indicate transfer complete.
 */
static void 
ifx_spi_receive_data(struct ifx_spi_data *spi_data)
{
	unsigned int rx_valid_buf_size;
	int status = 0; 

	//status = ifx_spi_sync_read(spi_data, spi_data->ifx_current_frame_size+IFX_SPI_HEADER_SIZE); /* 4 bytes for header */                       
	status = ifx_spi_sync_read(spi_data, IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE); /* 4 bytes for header */

	if(status > 0) //Status is not clear, I think in case of something TX this should be m.actual_length
	{
		memset(spi_data->ifx_tx_buffer,0,IFX_SPI_MAX_BUF_SIZE+IFX_SPI_HEADER_SIZE);
		//spi_data->ifx_ret_count += spi_data->ifx_valid_frame_size;
		// deinsala if Ok just set the ifx_ret_count to what I wrote (i.e.ifx_spi_count)
		spi_data->ifx_ret_count = spi_data->ifx_spi_count;
	}
	else
	{
		printk("[EBS-SPI] RX Failure !! STATUS = %d\n", status);
		return;
	}
	ifx_spi_tty_callback(spi_data);
}


/*
 * Function copies the TX_BUFFER and RX_BUFFER pointer to a spi_transfer structure and add it to SPI tasks.
 * And calls SPI Driver function "spi_sync" to start data transmission and reception to from MODEM
 */
static unsigned int 
ifx_spi_sync_read_write(struct ifx_spi_data *spi_data, unsigned int len)
{
	int status;
	struct spi_message	m;
	struct spi_transfer	t = {
						.tx_buf		= spi_data->ifx_tx_buffer,
                        .rx_buf		= spi_data->ifx_rx_buffer,
						.len		= len,
					};
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	if (spi_data->spi == NULL)
		status = -ESHUTDOWN;
	else
		status = spi_sync(spi_data->spi, &m);
	
	if (status == 0){          
		status = m.status;
		if (status == 0)
			status = m.actual_length;
	}
        else{
		printk("File: ifx_n721_spi.c\nFunction: unsigned int ifx_spi_sync\nTransmission UNsuccessful\n");
        }
#ifdef ETJO_DUMP
	dump_spi_smart(spi_data);	
#endif /* ETJO_DUMP */
	return status;
}

static unsigned int 
ifx_spi_sync_read(struct ifx_spi_data *spi_data, unsigned int len)
{
	int status;
	struct spi_message	m;
	struct spi_transfer	t = {
		.tx_buf		= NULL,
		.rx_buf		= spi_data->ifx_rx_buffer,
		.len		= len,
	};
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	
	if (spi_data->spi == NULL)
		status = -ESHUTDOWN;
	else
		status = spi_sync(spi_data->spi, &m);
	
	if (status == 0){          
		status = m.status;
		if (status == 0)
			status = m.actual_length;
	}
        else{
		printk("File: ifx_n721_spi.c\nFunction: unsigned int ifx_spi_sync\nTransmission UNsuccessful\n");
        }
#ifdef ETJO_DUMP
	dump_spi_smart(spi_data);	
#endif /* ETJO_DUMP */
	return status;
}


/*
 * Function is a Interrupt service routine, is called when SRDY signal goes HIGH. It set up transmission and
 * reception if it is a Slave initiated data transfer. For both the cases Master intiated/Slave intiated
 * transfer it starts data transfer. 
 */
static irqreturn_t 
ifx_spi_handle_srdy_irq(int irq, void *handle)
{
	int status;
	struct ifx_spi_data *spi_data = (struct ifx_spi_data *)handle;
		
#ifdef SPI_LOG_ENABLE_SHIM
	printk("\n\n%s: MRDY FROM MDM6600\n", __FUNCTION__);
#endif
	if(!spi_data) {
		printk("CP_RDY ISR, spi_data is null\n");
		return IRQ_HANDLED; //deinmawo - TODO should be an error indication!
	}
#ifdef	WAKE_LOCK_RESUME
		if(&spi_data_table[3]->wake_lock)
		{
			wake_lock_timeout(&spi_data_table[3]->wake_lock, 5);	//                                                                   
		}
#endif		

	status = queue_work(spi_data->ifx_wq, &spi_data->ifx_work);
#ifdef SPI_LOG_ENABLE_SHIM
	if(status == 0) {
		printk("CP_RDY ISR, work was already queued\n");
	}
#endif /* SPI_LOG_ENABLE_SHIM */

	//SPI_DEBUG_PRINT("%s-%d\n",__FUNCTION__, __LINE__);

	return IRQ_HANDLED; 
}
//                                                      
// TODO: 1) ifx_master_initiated_transfer = 1; -->WHEN THE SPI WRITE function calls
// TODO: 2) ifx_master_initiated_transfer = 0; -->Default 
//                                                      
static void 
ifx_spi_handle_work(struct work_struct *work)
{
	struct ifx_spi_data *spi_data = container_of(work, struct ifx_spi_data, ifx_work);
	int pm_off_count;

	// Here is CP initiated transfer
	if(!spi_data) return;
	mutex_lock(&mspi_tx_rx_mutex);
#ifdef EBS_TEST
       pm_off_count = 0;

   //need to wait transferring tx/rx data because ap is in a suspended state
   if(1 == is_suspended)
   {
          pm_off_count = 1;
          printk("[EBS] mdm_spi_handle_work INFO is_suspended is (0x%x)\n", is_suspended);

          //wait for ap to return to resume state with a worst case scenario of 5sec
          do
          {
                  mdelay(1);
                  pm_off_count++;

          }while((1 == is_suspended) && (pm_off_count<(5*200)));

          printk("[EBS] mdm_spi_handle_work INFO EXIT is_suspend = 0x%x pm_off_count=%d\n", is_suspended, pm_off_count);

          if(1 == is_suspended)
          {
                 // To Do how to handle the PM OFF state during 1sec
                 printk("[EBS] mdm_spi_handle_work error is_suspended is (0x%x)\n",is_suspended);
          }
   }
#endif /* EBS_TEST */

#ifdef SPI_LOG_ENABLE_SHIM
	printk("[IFX_SPI_WRITE] - CP - [S] \n");
#endif
//	omap_pm_set_max_sdma_lat(&pm_qos_handle_for_spi,10);
//                                                                                                                      

	ifx_spi_buffer_initialization(spi_data);	
	//ifx_spi_setup_transmission(spi_data);
	ifx_spi_setup_receive(spi_data);
	
	//mutex_lock(&mspi_tx_rx_mutex);
	ifx_spi_receive_data(spi_data);
//	omap_pm_set_max_sdma_lat(&pm_qos_handle_for_spi,-1);
//                                                                                                   

	
#ifdef SPI_LOG_ENABLE_SHIM
	printk("[IFX_SPI_WRITE] - CP - [END] \n");
#endif
	mutex_unlock(&mspi_tx_rx_mutex);
	
#if 0	
	if (!spi_data->ifx_master_initiated_transfer){

#ifdef CONFIG_SPI_DEBUG
		printk("[LGE-SPI] INTERRUPT BY MODEM\n");
#endif

		ifx_spi_setup_transmission(spi_data);
#ifdef CONFIG_LGE_SPI_MODE_SLAVE
#else
		ifx_spi_set_mrdy_signal(spi_data, 1);
#endif
		ifx_spi_send_and_receive_data(spi_data);
		
		//ifx_spi_set_mrdy_signal(spi_data, 0);
		/* Once data transmission is completed, the MRDY signal is lowered */
		if((spi_data->ifx_sender_buf_size == 0)  && (spi_data->ifx_receiver_buf_size == 0)){
			//ifx_spi_set_mrdy_signal(spi_data, 0);
			ifx_spi_buffer_initialization(spi_data);
		}

		/* We are processing the slave initiated transfer in the mean time Mux has requested master initiated data transfer */
		/* Once Slave initiated transfer is complete then start Master initiated transfer */
		if(spi_data->ifx_master_initiated_transfer == 1){
		/* It is a condition where Slave has initiated data transfer and both SRDY and MRDY are high and at the end of data transfer		
	 	* MUX has some data to transfer. MUX initiates Master initiated transfer rising MRDY high, which will not be detected at Slave-MODEM.
	 	* So it was required to rise MRDY high again */
#ifdef CONFIG_LGE_SPI_MODE_SLAVE
#else
                ifx_spi_set_mrdy_signal(spi_data, 1);    		
#endif
		}
	}
	else{
#ifdef CONFIG_SPI_DEBUG
		printk("[LGE-SPI] INTERRUPT BY OMAP\n");
#endif
		ifx_spi_setup_transmission(spi_data);     
	//	SPI_DEBUG_PRINT("%s[%d] : %x %x %x %x\n", __FUNCTION__, __LINE__, ifx_tx_buffer[10], ifx_tx_buffer[11], ifx_tx_buffer[12], ifx_tx_buffer[13] );
		ifx_spi_send_and_receive_data(spi_data);
		/* Once data transmission is completed, the MRDY signal is lowered */
		if(spi_data->ifx_sender_buf_size == 0)
		{
			if(spi_data->ifx_receiver_buf_size == 0)
			{		
				//ifx_spi_set_mrdy_signal(spi_data, 0);
				ifx_spi_buffer_initialization(spi_data);
			}
			else
			{
				//ifx_spi_set_mrdy_signal(spi_data, 0);
				ifx_spi_buffer_initialization(spi_data);
			}
			spi_data->ifx_master_initiated_transfer = 0;
			complete(&spi_data->ifx_read_write_completion);
		}
		else
		{
			
			//ifx_spi_set_mrdy_signal(spi_data, 0);
			ifx_spi_buffer_initialization(spi_data);
			spi_data->ifx_master_initiated_transfer = 0;
			complete(&spi_data->ifx_read_write_completion);
		}
		//ifx_spi_set_mrdy_signal(spi_data, 0);
	}
#endif //if 0
}


/* ################################################################################################################ */


/* ################################################################################################################ */

/* Initialization Functions */

/*
 * Initialization function which allocates and set different parameters for TTY SPI driver. Registers the tty driver 
 * with TTY core and register SPI driver with the Kernel. It validates the GPIO pins for MRDY and then request an IRQ
 * on SRDY GPIO pin for SRDY signal going HIGH. In case of failure of SPI driver register cases it unregister tty driver
 * from tty core.
 */
static int 
__init ifx_spi_init(void)
{
int status = 0;

	/* Allocate and Register a TTY device */
	ifx_spi_tty_driver = alloc_tty_driver(IFX_N_SPI_MINORS);
	if (!ifx_spi_tty_driver){
		printk(KERN_ERR "Fail to allocate TTY Driver\n");
		return -ENOMEM;
	}
//	printk("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@mdm6600_spi_init [start] @@@@@@@@@@@@@@@@@@@@\n");
	/* initialize the tty driver */
	ifx_spi_tty_driver->owner = THIS_MODULE;
	ifx_spi_tty_driver->driver_name = "ifxn721";
	ifx_spi_tty_driver->name = "ttyspi";
	ifx_spi_tty_driver->major = IFX_SPI_MAJOR;
	ifx_spi_tty_driver->minor_start = 0;
	ifx_spi_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	ifx_spi_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	ifx_spi_tty_driver->flags = TTY_DRIVER_REAL_RAW;
	ifx_spi_tty_driver->init_termios = tty_std_termios;
	ifx_spi_tty_driver->init_termios.c_cflag = B115200 | CS8 | CREAD | HUPCL | CLOCAL;//B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_set_operations(ifx_spi_tty_driver, &ifx_spi_ops);

	status = tty_register_driver(ifx_spi_tty_driver);
	if (status){
		printk(KERN_ERR "Failed to register IFX SPI tty driver");
		put_tty_driver(ifx_spi_tty_driver);
		return status;
	}
//	printk("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@mdm6600_spi_init - [before register driver] @@@@@@@@@@@@@@@@@@@@\n");
	/* Register SPI Driver */
	status = spi_register_driver(&ifx_spi_driver);
//		printk("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@mdm6600_spi_init - [register driver] @@@@@@@@@@@@@@@@@@@@\n");
	if (status < 0){ 
		printk("Failed to register SPI device");
		tty_unregister_driver(ifx_spi_tty_driver);
		put_tty_driver(ifx_spi_tty_driver);
		return status;
	}
//	printk("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@mdm6600_spi_init [end] @@@@@@@@@@@@@@@@@@@@\n");
	return status;
}

module_init(ifx_spi_init);


/*
 * Exit function to unregister SPI driver and tty SPI driver
 */
static void 
__exit ifx_spi_exit(void)
{  
	spi_unregister_driver(&ifx_spi_driver);
	tty_unregister_driver(ifx_spi_tty_driver);
        put_tty_driver(ifx_spi_tty_driver);
}

module_exit(ifx_spi_exit);

/* End of Initialization Functions */

/* ################################################################################################################ */

MODULE_AUTHOR("Umesh Bysani and Shreekanth D.H, <bysani@ti.com> <sdh@ti.com>");
MODULE_DESCRIPTION("IFX SPI Framing Layer");
MODULE_LICENSE("GPL");
