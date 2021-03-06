#
# Makefile for the touchscreen drivers.
#

# Each configuration option enables a list of files.

wm97xx-ts-y := wm97xx-core.o

obj-$(CONFIG_TOUCHSCREEN_88PM860X)	+= 88pm860x-ts.o
obj-$(CONFIG_TOUCHSCREEN_AD7877)	+= ad7877.o
obj-$(CONFIG_TOUCHSCREEN_AD7879)	+= ad7879.o
obj-$(CONFIG_TOUCHSCREEN_AD7879_I2C)	+= ad7879-i2c.o
obj-$(CONFIG_TOUCHSCREEN_AD7879_SPI)	+= ad7879-spi.o
obj-$(CONFIG_TOUCHSCREEN_ADS7846)	+= ads7846.o
obj-$(CONFIG_TOUCHSCREEN_ATMEL_MXT)	+= atmel_mxt_ts.o
obj-$(CONFIG_TOUCHSCREEN_ATMEL_TSADCC)	+= atmel_tsadcc.o
obj-$(CONFIG_TOUCHSCREEN_BITSY)		+= h3600_ts_input.o
obj-$(CONFIG_TOUCHSCREEN_BU21013)       += bu21013_ts.o
obj-$(CONFIG_TOUCHSCREEN_CY8CTMG110)	+= cy8ctmg110_ts.o
obj-$(CONFIG_TOUCHSCREEN_DA9034)	+= da9034-ts.o
obj-$(CONFIG_TOUCHSCREEN_DYNAPRO)	+= dynapro.o
obj-$(CONFIG_TOUCHSCREEN_HAMPSHIRE)	+= hampshire.o
obj-$(CONFIG_TOUCHSCREEN_GUNZE)		+= gunze.o
obj-$(CONFIG_TOUCHSCREEN_EETI)		+= eeti_ts.o
obj-$(CONFIG_TOUCHSCREEN_ELO)		+= elo.o
obj-$(CONFIG_TOUCHSCREEN_FUJITSU)	+= fujitsu_ts.o
obj-$(CONFIG_TOUCHSCREEN_INEXIO)	+= inexio.o
obj-$(CONFIG_TOUCHSCREEN_INTEL_MID)	+= intel-mid-touch.o
obj-$(CONFIG_TOUCHSCREEN_LPC32XX)	+= lpc32xx_ts.o
obj-$(CONFIG_TOUCHSCREEN_MAX11801)	+= max11801_ts.o
obj-$(CONFIG_TOUCHSCREEN_MC13783)	+= mc13783_ts.o
obj-$(CONFIG_TOUCHSCREEN_MCS5000)	+= mcs5000_ts.o
obj-$(CONFIG_TOUCHSCREEN_MIGOR)		+= migor_ts.o
obj-$(CONFIG_TOUCHSCREEN_MTOUCH)	+= mtouch.o
obj-$(CONFIG_TOUCHSCREEN_MK712)		+= mk712.o
obj-$(CONFIG_TOUCHSCREEN_HP600)		+= hp680_ts_input.o
obj-$(CONFIG_TOUCHSCREEN_HP7XX)		+= jornada720_ts.o
obj-$(CONFIG_TOUCHSCREEN_HTCPEN)	+= htcpen.o
obj-$(CONFIG_TOUCHSCREEN_USB_COMPOSITE)	+= usbtouchscreen.o
obj-$(CONFIG_TOUCHSCREEN_PCAP)		+= pcap_ts.o
obj-$(CONFIG_TOUCHSCREEN_PENMOUNT)	+= penmount.o
obj-$(CONFIG_TOUCHSCREEN_S3C2410)	+= s3c2410_ts.o
obj-$(CONFIG_TOUCHSCREEN_ST1232)	+= st1232.o
obj-$(CONFIG_TOUCHSCREEN_STMPE)		+= stmpe-ts.o
obj-$(CONFIG_TOUCHSCREEN_TNETV107X)	+= tnetv107x-ts.o
obj-$(CONFIG_TOUCHSCREEN_SYNAPTICS_I2C_RMI)	+= synaptics_i2c_rmi.o
obj-$(CONFIG_TOUCHSCREEN_TOUCHIT213)	+= touchit213.o
obj-$(CONFIG_TOUCHSCREEN_TOUCHRIGHT)	+= touchright.o
obj-$(CONFIG_TOUCHSCREEN_TOUCHWIN)	+= touchwin.o
obj-$(CONFIG_TOUCHSCREEN_TSC2005)	+= tsc2005.o
obj-$(CONFIG_TOUCHSCREEN_TSC2007)	+= tsc2007.o
obj-$(CONFIG_TOUCHSCREEN_UCB1400)	+= ucb1400_ts.o
obj-$(CONFIG_TOUCHSCREEN_WACOM_W8001)	+= wacom_w8001.o
obj-$(CONFIG_TOUCHSCREEN_WM831X)	+= wm831x-ts.o
obj-$(CONFIG_TOUCHSCREEN_WM97XX)	+= wm97xx-ts.o
wm97xx-ts-$(CONFIG_TOUCHSCREEN_WM9705)	+= wm9705.o
wm97xx-ts-$(CONFIG_TOUCHSCREEN_WM9712)	+= wm9712.o
wm97xx-ts-$(CONFIG_TOUCHSCREEN_WM9713)	+= wm9713.o
obj-$(CONFIG_TOUCHSCREEN_WM97XX_ATMEL)	+= atmel-wm97xx.o
obj-$(CONFIG_TOUCHSCREEN_WM97XX_MAINSTONE)	+= mainstone-wm97xx.o
obj-$(CONFIG_TOUCHSCREEN_WM97XX_ZYLONITE)	+= zylonite-wm97xx.o
obj-$(CONFIG_TOUCHSCREEN_W90X900)	+= w90p910_ts.o
obj-$(CONFIG_TOUCHSCREEN_TPS6507X)	+= tps6507x-ts.o
obj-$(CONFIG_TOUCHSCREEN_SYNTM12XX)	+= syntm12xx.o
obj-$(CONFIG_TOUCHSCREEN_QUANTUM_OBP)	+= qtouch_obp_ts.o

## LGE_SJIT_S 2011-10-13 [choongryeol.lee@lge.com] synaptics rmi4 touch driver
obj-$(CONFIG_LGE_TOUCHSCREEN_SYNAPTICS_RMI4_I2C)	+=touch_synaptics_rmi4_i2c.o touch_synaptics_fw_upgrade_lghdk.o
## LGE_SJIT_E 2011-10-13 [choongryeol.lee@lge.com] synaptics rmi4 touch driver

## FIXME - Should merge lg's driver ...
obj-$(CONFIG_TOUCHSCREEN_COMMON_SYNAPTICS)   += lge_touch_core.o
obj-$(CONFIG_TOUCHSCREEN_COMMON_SYNAPTICS)   += touch_synaptics_s3000.o
obj-$(CONFIG_TOUCHSCREEN_COMMON_SYNAPTICS)   += touch_synaptics_fw_upgrade.o

obj-$(CONFIG_TOUCHSCREEN_COMMON_SYNAPTICS_S3200)   += lge_touch_core.o
obj-$(CONFIG_TOUCHSCREEN_COMMON_SYNAPTICS_S3200)   += touch_synaptics_s7020.o
obj-$(CONFIG_TOUCHSCREEN_COMMON_SYNAPTICS_S3200)   += touch_synaptics_fw_upgrade.o

## LGE_SJIT 2011-11-17 [dojip.kim@lge.com] add atmel touch driver
## from [G1-D1L][touch] Add Atmel Touch Driver
obj-$(CONFIG_TOUCHSCREEN_ATMEL_MAXT224E)     += atmel_ts.o

## LGE_SJIT 2011-11-16 [dojip.kim@lge.com] add melfas touch driver
## from [G1-COMMON][touch] Add Melfas Touch Driver
obj-$(CONFIG_TOUCHSCREEN_MMS136)        += melfas_ts.o mms136_ISP_download.o mms136_ISC_download.o

## LGE_SJIT 2011-11-16 [dojip.kim@lge.com] unified touch firmware version path
## from [G1-COMMON][touch] Unified Touch Firmware Version PATH
obj-$(CONFIG_TS_INFO_CLASS)             += ts_class.o
obj-$(CONFIG_TOUCHSCREEN_CYPRESS_TTSP) +=  cyttsp4_core.o cyttsp4_i2c.o
