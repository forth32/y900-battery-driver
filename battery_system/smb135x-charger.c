#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/rtc.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/qpnp/qpnp-adc.h>

#include "battery_core.h"
#include "charger_core.h"

// макросы для формирования битовых масок
#define SMB135X_BITS_PER_REG	8
#define _SMB135X_MASK(BITS, POS)  ((unsigned char)(((1 << (BITS)) - 1) << (POS)))
#define SMB135X_MASK(LEFT_BIT_POS, RIGHT_BIT_POS) _SMB135X_MASK((LEFT_BIT_POS)-(RIGHT_BIT_POS)+1, (RIGHT_BIT_POS))

// Регистры конфигурации чипа

/* Config registers */
#define CFG_3_REG			0x03
#define CHG_ITERM_50MA			0x08
#define CHG_ITERM_100MA			0x10
#define CHG_ITERM_150MA			0x18
#define CHG_ITERM_200MA			0x20
#define CHG_ITERM_250MA			0x28
#define CHG_ITERM_300MA			0x00
#define CHG_ITERM_500MA			0x30
#define CHG_ITERM_600MA			0x38
#define CHG_ITERM_MASK			SMB135X_MASK(5, 3)

#define CFG_4_REG			0x04
#define CHG_INHIBIT_MASK		SMB135X_MASK(7, 6)
#define CHG_INHIBIT_50MV_VAL		0x00
#define CHG_INHIBIT_100MV_VAL		0x40
#define CHG_INHIBIT_200MV_VAL		0x80
#define CHG_INHIBIT_300MV_VAL		0xC0

#define CFG_5_REG			0x05
#define RECHARGE_200MV_BIT		BIT(2)
#define USB_2_3_BIT			BIT(5)

#define CFG_C_REG			0x0C
#define USBIN_INPUT_MASK		SMB135X_MASK(4, 0)

#define CFG_D_REG			0x0D

#define CFG_E_REG			0x0E
#define POLARITY_100_500_BIT		BIT(2)
#define USB_CTRL_BY_PIN_BIT		BIT(1)

#define CFG_10_REG			0x11
#define DCIN_INPUT_MASK			SMB135X_MASK(4, 0)

#define CFG_11_REG			0x11
#define PRIORITY_BIT			BIT(7)

#define USBIN_DCIN_CFG_REG		0x12
#define USBIN_SUSPEND_VIA_COMMAND_BIT	BIT(6)

#define CFG_14_REG			0x14
#define CHG_EN_BY_PIN_BIT			BIT(7)
#define CHG_EN_ACTIVE_LOW_BIT		BIT(6)
#define PRE_TO_FAST_REQ_CMD_BIT		BIT(5)
#define DISABLE_CURRENT_TERM_BIT	BIT(3)
#define DISABLE_AUTO_RECHARGE_BIT	BIT(2)
#define EN_CHG_INHIBIT_BIT		BIT(0)

#define CFG_16_REG			0x16
#define SAFETY_TIME_EN_BIT		BIT(4)
#define SAFETY_TIME_MINUTES_MASK	SMB135X_MASK(3, 2)
#define SAFETY_TIME_MINUTES_SHIFT	2

#define CFG_17_REG			0x17
#define CHG_STAT_DISABLE_BIT		BIT(0)
#define CHG_STAT_ACTIVE_HIGH_BIT	BIT(1)
#define CHG_STAT_IRQ_ONLY_BIT		BIT(4)

#define CFG_19_REG			0x19
#define BATT_MISSING_ALGO_BIT		BIT(2)
#define BATT_MISSING_THERM_BIT		BIT(1)

#define CFG_1A_REG			0x1A
#define HOT_SOFT_VFLOAT_COMP_EN_BIT	BIT(3)
#define COLD_SOFT_VFLOAT_COMP_EN_BIT	BIT(2)

#define VFLOAT_REG			0x1E

#define VERSION1_REG			0x2A
#define VERSION1_MASK			SMB135X_MASK(7,	6)
#define VERSION1_SHIFT			6
#define VERSION2_REG			0x32
#define VERSION2_MASK			SMB135X_MASK(1,	0)
#define VERSION3_REG			0x34

/* Irq Config registers */
#define IRQ_CFG_REG			0x07
#define IRQ_BAT_HOT_COLD_HARD_BIT	BIT(7)
#define IRQ_BAT_HOT_COLD_SOFT_BIT	BIT(6)
#define IRQ_USBIN_UV_BIT		BIT(2)
#define IRQ_INTERNAL_TEMPERATURE_BIT	BIT(0)

#define IRQ2_CFG_REG			0x08
#define IRQ2_SAFETY_TIMER_BIT		BIT(7)
#define IRQ2_CHG_ERR_BIT		BIT(6)
#define IRQ2_CHG_PHASE_CHANGE_BIT	BIT(4)
#define IRQ2_CHG_INHIBIT_BIT		BIT(3)
#define IRQ2_POWER_OK_BIT		BIT(2)
#define IRQ2_BATT_MISSING_BIT		BIT(1)
#define IRQ2_VBAT_LOW_BIT		BIT(0)

#define IRQ3_CFG_REG			0x09
#define IRQ3_RID_DETECT_BIT		BIT(4)
#define IRQ3_SRC_DETECT_BIT		BIT(2)
#define IRQ3_DCIN_UV_BIT		BIT(0)

#define USBIN_OTG_REG			0x0F
#define OTG_CNFG_MASK			SMB135X_MASK(3,	2)
#define OTG_CNFG_PIN_CTRL		0x04
#define OTG_CNFG_COMMAND_CTRL		0x08
#define OTG_CNFG_AUTO_CTRL		0x0C

/* Command Registers */
#define CMD_I2C_REG			0x40
#define ALLOW_VOLATILE_BIT		BIT(6)

#define CMD_INPUT_LIMIT			0x41
#define USB_SHUTDOWN_BIT		BIT(6)
#define DC_SHUTDOWN_BIT			BIT(5)
#define USE_REGISTER_FOR_CURRENT	BIT(2)
#define USB_100_500_AC_MASK		SMB135X_MASK(1, 0)
#define USB_100_VAL			0x02
#define USB_500_VAL			0x00
#define USB_AC_VAL			0x01

#define CMD_CHG_REG			0x42
#define CMD_CHG_EN			BIT(1)
#define OTG_EN				BIT(0)

/* Status registers */
#define STATUS_1_REG			0x47
#define USING_USB_BIT			BIT(1)
#define USING_DC_BIT			BIT(0)

#define STATUS_4_REG			0x4A
#define BATT_NET_CHG_CURRENT_BIT	BIT(7)
#define BATT_LESS_THAN_2V		BIT(4)
#define CHG_HOLD_OFF_BIT		BIT(3)
#define CHG_TYPE_MASK			SMB135X_MASK(2, 1)
#define CHG_TYPE_SHIFT			1
#define BATT_NOT_CHG_VAL		0x0
#define BATT_PRE_CHG_VAL		0x1
#define BATT_FAST_CHG_VAL		0x2
#define BATT_TAPER_CHG_VAL		0x3
#define CHG_EN_BIT			BIT(0)

#define STATUS_5_REG			0x4B
#define CDP_BIT				BIT(7)
#define DCP_BIT				BIT(6)
#define OTHER_BIT			BIT(5)
#define SDP_BIT				BIT(4)
#define ACA_A_BIT			BIT(3)
#define ACA_B_BIT			BIT(2)
#define ACA_C_BIT			BIT(1)
#define ACA_DOCK_BIT			BIT(0)

#define STATUS_6_REG			0x4C
#define RID_FLOAT_BIT			BIT(3)
#define RID_A_BIT			BIT(2)
#define RID_B_BIT			BIT(1)
#define RID_C_BIT			BIT(0)

#define STATUS_8_REG			0x4E
#define USBIN_9V			BIT(5)
#define USBIN_UNREG			BIT(4)
#define USBIN_LV			BIT(3)
#define DCIN_9V				BIT(2)
#define DCIN_UNREG			BIT(1)
#define DCIN_LV				BIT(0)

#define STATUS_9_REG			0x4F
#define REV_MASK			SMB135X_MASK(3, 0)

// Регистры управления прерываниями
#define IRQ_A_REG			0x50
#define IRQ_A_HOT_HARD_BIT		BIT(6)
#define IRQ_A_COLD_HARD_BIT		BIT(4)
#define IRQ_A_HOT_SOFT_BIT		BIT(2)
#define IRQ_A_COLD_SOFT_BIT		BIT(0)

#define IRQ_B_REG			0x51
#define IRQ_B_BATT_TERMINAL_BIT		BIT(6)
#define IRQ_B_BATT_MISSING_BIT		BIT(4)
#define IRQ_B_VBAT_LOW_BIT		BIT(2)
#define IRQ_B_TEMPERATURE_BIT		BIT(0)

#define IRQ_C_REG			0x52
#define IRQ_C_TERM_BIT			BIT(0)

#define IRQ_D_REG			0x53
#define IRQ_D_TIMEOUT_BIT		BIT(2)

#define IRQ_E_REG			0x54
#define IRQ_E_DC_OV_BIT			BIT(6)
#define IRQ_E_DC_UV_BIT			BIT(4)
#define IRQ_E_USB_OV_BIT		BIT(2)
#define IRQ_E_USB_UV_BIT		BIT(0)

#define IRQ_F_REG			0x55
#define IRQ_F_POWER_OK_BIT		BIT(0)

#define IRQ_G_REG			0x56
#define IRQ_G_SRC_DETECT_BIT		BIT(6)

static struct of_device_id smb135x_match_table[];

//*******************************
//* Флаги ошибок реализации чипа
//*******************************
enum {
	WRKARND_USB100_BIT = BIT(0),
	WRKARND_APSD_FAIL = BIT(1),
};

//*******************************
//* Коды ревизий чипа
//*******************************
enum {
	REV_1 = 1,	/* Rev 1.0 */
	REV_1_1 = 2,	/* Rev 1.1 */
	REV_2 = 3,		/* Rev 2 */
	REV_2_1 = 5,	/* Rev 2.1 */
	REV_MAX,
};

//*******************************
//* Описатели ревизий чипа
//*******************************

static char *revision_str[] = {
	[REV_1] = "rev1",
	[REV_1_1] = "rev1.1",
	[REV_2] = "rev2",
	[REV_2_1] = "rev2.1",
};

//*******************************
//* Варианты чипа
//*******************************

enum {
	V_SMB1356,
	V_SMB1357,
	V_SMB1358,
	V_SMB1359,
	V_MAX,
};

static char *version_str[] = {
	[V_SMB1356] = "smb1356",
	[V_SMB1357] = "smb1357",
	[V_SMB1358] = "smb1358",
	[V_SMB1359] = "smb1359",
};

enum {
	USER = BIT(0),
	THERMAL = BIT(1),
	CURRENT = BIT(2),
};

enum path_type {
	USB,
	DC,
};

//**************************************
//*  Описатель регулятора напряжения
//**************************************
struct smb135x_regulator {
	struct regulator_desc	rdesc;
	struct regulator_dev	*rdev;
};

//**************************************
//*  массив значений времени зарядки
//**************************************
static int chg_time[] = {
	192,
	384,
	768,
	1536,
};

//**************************************
//*  массивы значений токов USB
//**************************************
static int usb_current_table_smb1356[] = {
	180,
	240,
	270,
	285,
	300,
	330,
	360,
	390,
	420,
	540,
	570,
	600,
	660,
	720,
	840,
	900,
	960,
	1080,
	1110,
	1128,
	1146,
	1170,
	1182,
	1200,
	1230,
	1260,
	1380,
	1440,
	1560,
	1620,
	1680,
	1800
};

static int usb_current_table_smb1357_smb1358[] = {
	300,
	400,
	450,
	475,
	500,
	550,
	600,
	650,
	700,
	900,
	950,
	1000,
	1100,
	1200,
	1400,
	1450,
	1500,
	1600,
	1800,
	1850,
	1880,
	1910,
	1930,
	1950,
	1970,
	2000,
	2050,
	2100,
	2300,
	2400,
	2500,
	3000
};

static int usb_current_table_smb1359[] = {
	300,
	400,
	450,
	475,
	500,
	550,
	600,
	650,
	700,
	900,
	950,
	1000,
	1100,
	1200,
	1400,
	1450,
	1500,
	1600,
	1800,
	1850,
	1880,
	1910,
	1930,
	1950,
	1970,
	2000,
	2050,
	2100,
	2300,
	2400,
	2500
};

static int dc_current_table_smb1356[] = {
	180,
	240,
	270,
	285,
	300,
	330,
	360,
	390,
	420,
	540,
	570,
	600,
	660,
	720,
	840,
	870,
	900,
	960,
	1080,
	1110,
	1128,
	1146,
	1158,
	1170,
	1182,
	1200,
};

static int dc_current_table[] = {
	300,
	400,
	450,
	475,
	500,
	550,
	600,
	650,
	700,
	900,
	950,
	1000,
	1100,
	1200,
	1400,
	1450,
	1500,
	1600,
	1800,
	1850,
	1880,
	1910,
	1930,
	1950,
	1970,
	2000,
};

#define CURRENT_100_MA		100
#define CURRENT_150_MA		150
#define CURRENT_500_MA		500
#define CURRENT_900_MA		900
#define SUSPEND_CURRENT_MA	2

//**************************************
//*  Главная интерфейсная структура 
//**************************************
// размер 828 байт

struct smb135x_chg {

  struct i2c_client* client;       // 0
  struct device* dev;              //4
  struct mutex	read_write_lock;   //8, 40 байт
  struct charger_interface* api;  // +48 - API charger_interface  
  struct smb135x_chg* self;        // 92
  int (*set_current_limit_fn)(void *self, int mA);  //96
  int (*enable_charge_fn)(void *self, int enable); // 100
  int (*set_otg_mode_fn)(void *self, int enable); // 104
  char* ext_name_battery; // 108
  char* ext_name_usb;     // 112

  int cx128;
  
  int cx144;
  
  int cx160;
  
  u8	revision;    // 176
  int	version;     // 180
  bool	chg_enabled; // 184
  bool	usb_present; // 185
  bool	dc_present;  // 186
  bool	usb_slave_present;  //187
  bool	dc_ov;   // 188
  bool	bmd_algo_disabled;  // 189
  bool	iterm_disabled;     // 190
  int	iterm_ma;        // 192
  int	vfloat_mv;       // 196
  int	safety_time;     // 200
  int resume_delta_mv;   // 204
  int fake_battery_soc;  // 208
  struct dentry* debug_root;   // 212
  int usb_current_arr_size;  // 216
  int* usb_current_table;    // 220
  int	dc_current_arr_size;  // 224
  int	*dc_current_table;    // 228
  u8	irq_cfg_mask[3];     // 232
  struct power_supply* usb_psy;    //236
  int	usb_psy_ma;  // 240

  struct power_supply dc_psy;     // 352
  

  int	dc_psy_type;  // 464
  int	dc_psy_ma;    // 468
  const char* bms_psy_name; // 472

  bool	chg_done_batt_full;  // 476
  bool	batt_present;    // 477
  bool	batt_hot;  // 478
  bool	batt_cold; //479
  bool	batt_warm; //480
  bool	batt_cool; //481
  bool  temp_monitor_disabled; // 482
  bool  resume_completed;  // 483
  bool	irq_waiting;  // 484
  u32	usb_suspended; // 488
  u32	dc_suspended;  // 492
  struct mutex	path_suspend_lock;  // 496, 40 байт
  u32	peek_poke_address; // 536
  struct smb135x_regulator  otg_vreg; //540
   
  int	skip_writes; // 640
  int 	skip_reads;  //644
  u32   workaround_flags;          // 648
  bool	soft_vfloat_comp_disabled; // 652

  struct mutex	irq_complete; // 656, 40 байт
  struct regulator* therm_bias_vreg; //696
  struct delayed_work wireless_insertion_work;  // 700, размер 76 
  // 700: counter
  // 704-708 - entry
  // 712: work_func
  // 716:  struct timer_list timer;  размер 52
  //			  struct list_head entry; 716-720
  //			  unsigned long expires; 724
  //			  struct tvec_base *base; 728
   //             void (*function)(unsigned long); 732
   //             unsigned long data; 736
   //             int slack; 740
   //             int start_pid; 744
   //             void *start_site; 748
   //             char start_comm[16]; 752-768
   // struct workqueue_struct *wq; 772
   unsigned int	thermal_levels; //776
   unsigned int	therm_lvl_sel; // 780
   unsigned int* thermal_mitigation; // 784
   struct mutex	current_change_lock; // 788, 40 байт
//--- конец структуры
  
};
//############################################################################################
// прототип структуры из открытого модуля
/*  
struct smb135x_chg {
	struct i2c_client		*client;              // 0
	struct device			*dev;              //4
	struct mutex			read_write_lock;   //8, 40 байт

	u8				revision;    // 48
	int				version;     // 52

	bool				chg_enabled; // 56

	bool				usb_present; //60
	bool				dc_present;  // 64
	bool				usb_slave_present;  //68
	bool				dc_ov;   // 72

	bool				bmd_algo_disabled;  // 76
	bool				iterm_disabled;     // 80
	int				iterm_ma;          // 84
	int				vfloat_mv;        // 88
	int				safety_time;       //92
	int				resume_delta_mv;   // 96
	int				fake_battery_soc;  // 100
	struct dentry			*debug_root;       //104
	int				usb_current_arr_size;  // 108
	int				*usb_current_table;    // 112
	int				dc_current_arr_size;  // 116
	int				*dc_current_table;    // 120
	u8				irq_cfg_mask[3];     // 124

//	 psy 
	struct power_supply		*usb_psy;    //128
	int				usb_psy_ma;  //132
	struct power_supply		batt_psy;   //136, размер 148
	struct power_supply		dc_psy;     // 284
	struct power_supply		*bms_psy;   //432
	int				dc_psy_type;  // 436
	int				dc_psy_ma;    // 440
	const char			*bms_psy_name; // 444

//	 status tracking 
	bool				chg_done_batt_full;  // 448
	bool				batt_present;    // 452
	bool				batt_hot;
	bool				batt_cold;
	bool				batt_warm;
	bool				batt_cool;

	bool				resume_completed;
	bool				irq_waiting;
	u32				usb_suspended;
	u32				dc_suspended;
	struct mutex			path_suspend_lock;

	u32				peek_poke_address;
	struct smb135x_regulator	otg_vreg;
	int				skip_writes;
	int				skip_reads;
	u32				workaround_flags;
	bool				soft_vfloat_comp_disabled;
	struct mutex			irq_complete;
	struct regulator		*therm_bias_vreg;
	struct delayed_work		wireless_insertion_work;

	unsigned int			thermal_levels;
	unsigned int			therm_lvl_sel;
	unsigned int			*thermal_mitigation;
	struct mutex			current_change_lock;
};
*/


#define RETRY_COUNT 5
int retry_sleep_ms[RETRY_COUNT] = {
	10, 20, 30, 40, 50
};

//######## Низкоуровневые процедуры доступа к регистрам ##############3

//************************************************
//* Чтение регистра 
//************************************************
static int __smb135x_read(struct smb135x_chg *chip, int reg,u8 *val) {
	s32 ret;
	int retry_count = 0;

retry:
	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0 && retry_count < RETRY_COUNT) {
		/* sleep for few ms before retrying */
		msleep(retry_sleep_ms[retry_count++]);
		goto retry;
	}
	if (ret < 0) {
		dev_err(chip->dev,
			"i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	} else {
		*val = ret;
	}

	return 0;
}

//************************************************
//* Запись регистра 
//************************************************
static int __smb135x_write(struct smb135x_chg *chip, int reg,
						u8 val)
{
	s32 ret;
	int retry_count = 0;

retry:
	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0 && retry_count < RETRY_COUNT) {
		/* sleep for few ms before retrying */
		msleep(retry_sleep_ms[retry_count++]);
		goto retry;
	}
	if (ret < 0) {
		dev_err(chip->dev,
			"i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, ret);
		return ret;
	}
	pr_debug("Writing 0x%02x=0x%02x\n", reg, val);
	return 0;
}

//*****************************************************
//* Чтение регистра в режиме исключительного доступа
//*****************************************************
static int smb135x_read(struct smb135x_chg *chip, int reg,u8 *val) {
	int rc;

	if (chip->skip_reads) {
		*val = 0;
		return 0;
	}
	mutex_lock(&chip->read_write_lock);
	rc = __smb135x_read(chip, reg, val);
	mutex_unlock(&chip->read_write_lock);

	return rc;
}

//*****************************************************
//* Запись регистра в режиме исключительного доступа
//*****************************************************
static int smb135x_write(struct smb135x_chg *chip, int reg,u8 val) {
	int rc;

	if (chip->skip_writes)
		return 0;

	mutex_lock(&chip->read_write_lock);
	rc = __smb135x_write(chip, reg, val);
	mutex_unlock(&chip->read_write_lock);

	return rc;
}

//*******************************************************************
//* Запись отдельных битов регистра в режиме исключительного доступа
//*******************************************************************
static int smb135x_masked_write(struct smb135x_chg *chip, int reg,u8 mask, u8 val) {
	s32 rc;
	u8 temp;

	if (chip->skip_writes || chip->skip_reads)
		return 0;

	mutex_lock(&chip->read_write_lock);
	rc = __smb135x_read(chip, reg, &temp);
	if (rc < 0) {
		dev_err(chip->dev, "read failed: reg=%03X, rc=%d\n", reg, rc);
		goto out;
	}
	temp &= ~mask;
	temp |= val & mask;
	rc = __smb135x_write(chip, reg, temp);
	if (rc < 0) {
		dev_err(chip->dev,
			"write failed: reg=%03X, rc=%d\n", reg, rc);
	}
out:
	mutex_unlock(&chip->read_write_lock);
	return rc;
}

//**************************************
//* Проверка режима usb slave
//**************************************

static bool is_usb_slave_present(struct smb135x_chg *chip)
{
	bool usb_slave_present;
	u8 reg;
	int rc;

	rc = smb135x_read(chip, STATUS_6_REG, &reg);
	if (rc < 0) {
		pr_err("Couldn't read stat 6 rc = %d\n", rc);
		return false;
	}

	if ((reg & (RID_FLOAT_BIT | RID_A_BIT | RID_B_BIT | RID_C_BIT)) == 0)
		usb_slave_present = 1;
	else
		usb_slave_present = 0;

	pr_debug("stat6= 0x%02x slave_present = %d\n", reg, usb_slave_present);
	return usb_slave_present;
}

//**************************************
//* Таблица символьных описаний типа USB
//**************************************

static char *usb_type_str[] = {
	"ACA_DOCK",	/* bit 0 */
	"ACA_C",	/* bit 1 */
	"ACA_B",	/* bit 2 */
	"ACA_A",	/* bit 3 */
	"SDP",		/* bit 4 */
	"OTHER",	/* bit 5 */
	"DCP",		/* bit 6 */
	"CDP",		/* bit 7 */
	"NONE",		/* bit 8  error case */
};

//*******************************************
//* Получение символьного описания типа usb
//*******************************************
static char *get_usb_type_name(u8 stat_5) {
	unsigned long stat = stat_5;

	return usb_type_str[find_first_bit(&stat, SMB135X_BITS_PER_REG)];
}

//**************************************
//* Таблица битов соответствия типу USB
//**************************************
static enum power_supply_type usb_type_enum[] = {
	POWER_SUPPLY_TYPE_USB_ACA,	/* bit 0 */
	POWER_SUPPLY_TYPE_USB_ACA,	/* bit 1 */
	POWER_SUPPLY_TYPE_USB_ACA,	/* bit 2 */
	POWER_SUPPLY_TYPE_USB_ACA,	/* bit 3 */
	POWER_SUPPLY_TYPE_USB,		/* bit 4 */
	POWER_SUPPLY_TYPE_UNKNOWN,	/* bit 5 */
	POWER_SUPPLY_TYPE_USB_DCP,	/* bit 6 */
	POWER_SUPPLY_TYPE_USB_CDP,	/* bit 7 */
	POWER_SUPPLY_TYPE_UNKNOWN,	/* bit 8 error case, report UNKNWON */
};

/* helper to return enum power_supply_type of USB type */
static enum power_supply_type get_usb_supply_type(u8 stat_5)
{
	unsigned long stat = stat_5;

	return usb_type_enum[find_first_bit(&stat, SMB135X_BITS_PER_REG)];
}

//************************************************
//*
//************************************************
static int __smb135x_usb_suspend(struct smb135x_chg *chip, bool suspend) {
	int rc;

	rc = smb135x_masked_write(chip, CMD_INPUT_LIMIT,
			USB_SHUTDOWN_BIT, suspend ? USB_SHUTDOWN_BIT : 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set cfg 11 rc = %d\n", rc);
	return rc;
}

//************************************************
//*
//************************************************
static int __smb135x_dc_suspend(struct smb135x_chg *chip, bool suspend) {
	int rc = 0;

	rc = smb135x_masked_write(chip, CMD_INPUT_LIMIT,
			DC_SHUTDOWN_BIT, suspend ? DC_SHUTDOWN_BIT : 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set cfg 11 rc = %d\n", rc);
	return rc;
}

//************************************************
//*
//************************************************

static int smb135x_path_suspend(struct smb135x_chg *chip, enum path_type path,int reason, bool suspend) {
	int rc = 0;
	int suspended;
	int *path_suspended;
	int (*func)(struct smb135x_chg *chip, bool suspend);

	mutex_lock(&chip->path_suspend_lock);
	if (path == USB) {
		suspended = chip->usb_suspended;
		path_suspended = &chip->usb_suspended;
		func = __smb135x_usb_suspend;
	} else {
		suspended = chip->dc_suspended;
		path_suspended = &chip->dc_suspended;
		func = __smb135x_dc_suspend;
	}

	if (suspend == false)
		suspended &= ~reason;
	else
		suspended |= reason;

	if (*path_suspended && !suspended)
		rc = func(chip, 0);
	if (!(*path_suspended) && suspended)
		rc = func(chip, 1);

	if (rc)
		dev_err(chip->dev, "Couldn't set/unset suspend for %s path rc = %d\n",
					path == USB ? "usb" : "dc",
					rc);
	else
		*path_suspended = suspended;

	mutex_unlock(&chip->path_suspend_lock);
	return rc;
}


//************************************************
//* Обработчики событий
//************************************************
static int hot_hard_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	chip->batt_hot = !!rt_stat;
	return 0;
}
static int cold_hard_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	chip->batt_cold = !!rt_stat;
	return 0;
}
static int hot_soft_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	chip->batt_warm = !!rt_stat;
	return 0;
}
static int cold_soft_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	chip->batt_cool = !!rt_stat;
	return 0;
}
static int battery_missing_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	chip->batt_present = !rt_stat;
	return 0;
}
static int vbat_low_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_warn("vbat low\n");
	return 0;
}
static int chg_hot_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_warn("chg hot\n");
	return 0;
}
static int chg_term_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	chip->chg_done_batt_full = !!rt_stat;
	return 0;
}

static int taper_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	return 0;
}

static int fast_chg_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
//	power_supply_changed(&chip->batt_psy);
	return 0;
}

static int recharge_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	return 0;
}

static int safety_timeout_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_warn("safety timeout rt_stat = 0x%02x\n", rt_stat);
	return 0;
}

/**
 * power_ok_handler() - called when the switcher turns on or turns off
 * @chip: pointer to smb135x_chg chip
 * @rt_stat: the status bit indicating switcher turning on or off
 */
static int power_ok_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	return 0;
}

static int rid_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	bool usb_slave_present;

	usb_slave_present = is_usb_slave_present(chip);

	if (chip->usb_slave_present ^ usb_slave_present) {
		chip->usb_slave_present = usb_slave_present;
		if (chip->usb_psy) {
			pr_debug("setting usb psy usb_otg = %d\n",
					chip->usb_slave_present);
			power_supply_set_usb_otg(chip->usb_psy,
				chip->usb_slave_present);
		}
	}
	return 0;
}

static int handle_dc_removal(struct smb135x_chg *chip)
{
	if (chip->dc_psy_type == POWER_SUPPLY_TYPE_WIRELESS) {
		cancel_delayed_work_sync(&chip->wireless_insertion_work);
		smb135x_path_suspend(chip, DC, CURRENT, true);
	}

	if (chip->dc_psy_type != -EINVAL)
		power_supply_set_online(&chip->dc_psy, chip->dc_present);
	return 0;
}

#define DCIN_UNSUSPEND_DELAY_MS		1000
static int handle_dc_insertion(struct smb135x_chg *chip)
{
	if (chip->dc_psy_type == POWER_SUPPLY_TYPE_WIRELESS)
		schedule_delayed_work(&chip->wireless_insertion_work,
			msecs_to_jiffies(DCIN_UNSUSPEND_DELAY_MS));
	if (chip->dc_psy_type != -EINVAL)
		power_supply_set_online(&chip->dc_psy,
						chip->dc_present);

	return 0;
}
/**
 * dcin_uv_handler() - called when the dc voltage crosses the uv threshold
 * @chip: pointer to smb135x_chg chip
 * @rt_stat: the status bit indicating whether dc voltage is uv
 */
static int dcin_uv_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	/*
	 * rt_stat indicates if dc is undervolted. If so dc_present
	 * should be marked removed
	 */
	bool dc_present = !rt_stat;

	pr_debug("chip->dc_present = %d dc_present = %d\n",
			chip->dc_present, dc_present);

	if (chip->dc_present && !dc_present) {
		/* dc removed */
		chip->dc_present = dc_present;
		handle_dc_removal(chip);
	}

	if (!chip->dc_present && dc_present) {
		/* dc inserted */
		chip->dc_present = dc_present;
		handle_dc_insertion(chip);
	}

	return 0;
}

static int dcin_ov_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	/*
	 * rt_stat indicates if dc is overvolted. If so dc_present
	 * should be marked removed
	 */
	bool dc_present = !rt_stat;

	pr_debug("chip->dc_present = %d dc_present = %d\n",
			chip->dc_present, dc_present);

	chip->dc_ov = !!rt_stat;

	if (chip->dc_present && !dc_present) {
		/* dc removed */
		chip->dc_present = dc_present;
		handle_dc_removal(chip);
	}

	if (!chip->dc_present && dc_present) {
		/* dc inserted */
		chip->dc_present = dc_present;
		handle_dc_insertion(chip);
	}
	return 0;
}

//*******************************************************
//*  Отключение USB-кабеля
//*******************************************************

static int handle_usb_removal(struct smb135x_chg *chip)
{
	if (chip->usb_psy) {
		pr_debug("setting usb psy type = %d\n",
				POWER_SUPPLY_TYPE_UNKNOWN);
		power_supply_set_supply_type(chip->usb_psy,
				POWER_SUPPLY_TYPE_UNKNOWN);
		pr_debug("setting usb psy present = %d\n", chip->usb_present);
		power_supply_set_present(chip->usb_psy, chip->usb_present);
	}
	return 0;
}
//*******************************************************
//* Подключение USB-кабеля
//*******************************************************

static int handle_usb_insertion(struct smb135x_chg *chip)
{
	u8 reg;
	int rc;
	char *usb_type_name = "null";
	enum power_supply_type usb_supply_type;

	/* usb inserted */
	rc = smb135x_read(chip, STATUS_5_REG, &reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read status 5 rc = %d\n", rc);
		return rc;
	}
	/*
	 * Report the charger type as UNKNOWN if the
	 * apsd-fail flag is set. This nofifies the USB driver
	 * to initiate a s/w based charger type detection.
	 */
	if (chip->workaround_flags & WRKARND_APSD_FAIL)
		reg = 0;

	usb_type_name = get_usb_type_name(reg);
	usb_supply_type = get_usb_supply_type(reg);
	pr_debug("inserted %s, usb psy type = %d stat_5 = 0x%02x\n",
			usb_type_name, usb_supply_type, reg);
	if (chip->usb_psy) {
		pr_debug("setting usb psy type = %d\n", usb_supply_type);
		power_supply_set_supply_type(chip->usb_psy, usb_supply_type);
		pr_debug("setting usb psy present = %d\n", chip->usb_present);
		power_supply_set_present(chip->usb_psy, chip->usb_present);
	}
	return 0;
}

/**
 * usbin_uv_handler() - this is called when USB charger is removed
 * @chip: pointer to smb135x_chg chip
 * @rt_stat: the status bit indicating chg insertion/removal
 */
static int usbin_uv_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	/*
	 * rt_stat indicates if usb is undervolted. If so usb_present
	 * should be marked removed
	 */
	bool usb_present = !rt_stat;

	pr_debug("chip->usb_present = %d usb_present = %d\n",
			chip->usb_present, usb_present);
	if (chip->usb_present && !usb_present) {
		/* USB removed */
		chip->usb_present = usb_present;
		handle_usb_removal(chip);
	}
	return 0;
}

static int usbin_ov_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	/*
	 * rt_stat indicates if usb is overvolted. If so usb_present
	 * should be marked removed
	 */
	bool usb_present = !rt_stat;
	int health;

	pr_debug("chip->usb_present = %d usb_present = %d\n",
			chip->usb_present, usb_present);
	if (chip->usb_present && !usb_present) {
		/* USB removed */
		chip->usb_present = usb_present;
		handle_usb_removal(chip);
	}

	if (chip->usb_psy) {
		health = rt_stat ? POWER_SUPPLY_HEALTH_OVERVOLTAGE
					: POWER_SUPPLY_HEALTH_GOOD;
		power_supply_set_health_state(chip->usb_psy, health);
	}

	return 0;
}

/**
 * src_detect_handler() - this is called when USB charger type is detected, use
 *			it for handling USB charger insertion/removal
 * @chip: pointer to smb135x_chg chip
 * @rt_stat: the status bit indicating chg insertion/removal
 */
static int src_detect_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	bool usb_present = !!rt_stat;

	pr_debug("chip->usb_present = %d usb_present = %d\n",
			chip->usb_present, usb_present);

	if (!chip->usb_present && usb_present) {
		/* USB inserted */
		chip->usb_present = usb_present;
		handle_usb_insertion(chip);
	}

	return 0;
}

static int chg_inhibit_handler(struct smb135x_chg *chip, u8 rt_stat)
{
	/*
	 * charger is inserted when the battery voltage is high
	 * so h/w won't start charging just yet. Treat this as
	 * battery full
	 */
	pr_debug("rt_stat = 0x%02x\n", rt_stat);
	chip->chg_done_batt_full = !!rt_stat;
	return 0;
}

struct smb_irq_info {
	const char		*name;
	int			(*smb_irq)(struct smb135x_chg *chip,
							u8 rt_stat);
	int			high;
	int			low;
};

struct irq_handler_info {
	u8			stat_reg;
	u8			val;
	u8			prev_val;
	struct smb_irq_info	irq_info[4];
};

static struct irq_handler_info handlers[] = {
	{IRQ_A_REG, 0, 0,
		{
			{
				.name		= "cold_soft",
				.smb_irq	= cold_soft_handler,
			},
			{
				.name		= "hot_soft",
				.smb_irq	= hot_soft_handler,
			},
			{
				.name		= "cold_hard",
				.smb_irq	= cold_hard_handler,
			},
			{
				.name		= "hot_hard",
				.smb_irq	= hot_hard_handler,
			},
		},
	},
	{IRQ_B_REG, 0, 0,
		{
			{
				.name		= "chg_hot",
				.smb_irq	= chg_hot_handler,
			},
			{
				.name		= "vbat_low",
				.smb_irq	= vbat_low_handler,
			},
			{
				.name		= "battery_missing",
				.smb_irq	= battery_missing_handler,
			},
			{
				.name		= "battery_missing",
				.smb_irq	= battery_missing_handler,
			},
		},
	},
	{IRQ_C_REG, 0, 0,
		{
			{
				.name		= "chg_term",
				.smb_irq	= chg_term_handler,
			},
			{
				.name		= "taper",
				.smb_irq	= taper_handler,
			},
			{
				.name		= "recharge",
				.smb_irq	= recharge_handler,
			},
			{
				.name		= "fast_chg",
				.smb_irq	= fast_chg_handler,
			},
		},
	},
	{IRQ_D_REG, 0, 0,
		{
			{
				.name		= "prechg_timeout",
			},
			{
				.name		= "safety_timeout",
				.smb_irq	= safety_timeout_handler,
			},
			{
				.name		= "aicl_done",
			},
			{
				.name		= "battery_ov",
			},
		},
	},
	{IRQ_E_REG, 0, 0,
		{
			{
				.name		= "usbin_uv",
				.smb_irq	= usbin_uv_handler,
			},
			{
				.name		= "usbin_ov",
				.smb_irq	= usbin_ov_handler,
			},
			{
				.name		= "dcin_uv",
				.smb_irq	= dcin_uv_handler,
			},
			{
				.name		= "dcin_ov",
				.smb_irq	= dcin_ov_handler,
			},
		},
	},
	{IRQ_F_REG, 0, 0,
		{
			{
				.name		= "power_ok",
				.smb_irq	= power_ok_handler,
			},
			{
				.name		= "rid",
				.smb_irq	= rid_handler,
			},
			{
				.name		= "otg_fail",
			},
			{
				.name		= "otg_oc",
			},
		},
	},
	{IRQ_G_REG, 0, 0,
		{
			{
				.name		= "chg_inhibit",
				.smb_irq	= chg_inhibit_handler,
			},
			{
				.name		= "chg_error",
			},
			{
				.name		= "wd_timeout",
			},
			{
				.name		= "src_detect",
				.smb_irq	= src_detect_handler,
			},
		},
	},
};

//************************************************
//*
//************************************************
static int smb135x_irq_read(struct smb135x_chg *chip) {
	int rc, i;

	/*
	 * When dcin path is suspended the irq triggered status is not cleared
	 * causing a storm. To prevent this situation unsuspend dcin path while
	 * reading interrupts and restore its status back.
	 */
	mutex_lock(&chip->path_suspend_lock);

	if (chip->dc_suspended)
		__smb135x_dc_suspend(chip, false);

	for (i = 0; i < ARRAY_SIZE(handlers); i++) {
		rc = smb135x_read(chip, handlers[i].stat_reg,
						&handlers[i].val);
		if (rc < 0) {
			dev_err(chip->dev, "Couldn't read %d rc = %d\n",
					handlers[i].stat_reg, rc);
			handlers[i].val = 0;
			continue;
		}
	}

	if (chip->dc_suspended)
		__smb135x_dc_suspend(chip, true);

	mutex_unlock(&chip->path_suspend_lock);

	return rc;
}
#define IRQ_LATCHED_MASK	0x02
#define IRQ_STATUS_MASK		0x01
#define BITS_PER_IRQ		2

//**************************************
//* Обработчик прерывания от smb135x
//**************************************
static irqreturn_t smb135x_chg_stat_handler(int irq, void *dev_id) {
struct smb135x_chg *chip = dev_id;
int i, j;
u8 triggered;
u8 changed;
u8 rt_stat, prev_rt_stat;
int rc;
int handler_count = 0;

mutex_lock(&chip->irq_complete);
chip->irq_waiting = true;
if (!chip->resume_completed) {
	dev_dbg(chip->dev, "IRQ triggered before device-resume\n");
	disable_irq_nosync(irq);
	mutex_unlock(&chip->irq_complete);
	return IRQ_HANDLED;
}
chip->irq_waiting = false;

smb135x_irq_read(chip);
for (i = 0; i < ARRAY_SIZE(handlers); i++) {
	for (j = 0; j < ARRAY_SIZE(handlers[i].irq_info); j++) {
		triggered = handlers[i].val
		       & (IRQ_LATCHED_MASK << (j * BITS_PER_IRQ));
		rt_stat = handlers[i].val
			& (IRQ_STATUS_MASK << (j * BITS_PER_IRQ));
		prev_rt_stat = handlers[i].prev_val
			& (IRQ_STATUS_MASK << (j * BITS_PER_IRQ));
		changed = prev_rt_stat ^ rt_stat;

		if (triggered || changed)
			rt_stat ? handlers[i].irq_info[j].high++ :
					handlers[i].irq_info[j].low++;

		if ((triggered || changed)
			&& handlers[i].irq_info[j].smb_irq != NULL) {
			handler_count++;
			rc = handlers[i].irq_info[j].smb_irq(chip,
							rt_stat);
			if (rc < 0)
				dev_err(chip->dev,
					"Couldn't handle %d irq for reg 0x%02x rc = %d\n",
					j, handlers[i].stat_reg, rc);
		}
	}
	handlers[i].prev_val = handlers[i].val;
}

pr_debug("handler count = %d\n", handler_count);
/*/
if (handler_count) {
	pr_debug("batt psy changed\n");
	power_supply_changed(&chip->batt_psy);
	if (chip->usb_psy) {
		pr_debug("usb psy changed\n");
		power_supply_changed(chip->usb_psy);
	}
	if (chip->dc_psy_type != -EINVAL) {
		pr_debug("dc psy changed\n");
		power_supply_changed(&chip->dc_psy);
	}
}
*/
mutex_unlock(&chip->irq_complete);

return IRQ_HANDLED;
}


//**************************************
//* Процедуры включения зарядки
//**************************************
static int __smb135x_charging(struct smb135x_chg *chip, int enable)
{
	int rc = 0;

	pr_debug("charging enable = %d\n", enable);

	rc = smb135x_masked_write(chip, CMD_CHG_REG,
			CMD_CHG_EN, enable ? CMD_CHG_EN : 0);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't set CHG_ENABLE_BIT enable = %d rc = %d\n",
			enable, rc);
		return rc;
	}
	chip->chg_enabled = enable;

	/* set the suspended status */
	rc = smb135x_path_suspend(chip, DC, USER, !enable);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't set dc suspend to %d rc = %d\n",
			enable, rc);
		return rc;
	}
	rc = smb135x_path_suspend(chip, USB, USER, !enable);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't set usb suspend to %d rc = %d\n",
			enable, rc);
		return rc;
	}

	pr_debug("charging %s\n",
			enable ?  "enabled" : "disabled running from batt");
	return rc;
}
//*****************************************
//* Верхняя процедура включения зарядки
//*****************************************

static int smb135x_charging(struct smb135x_chg *chip, int enable) {
	int rc = 0;

	pr_debug("charging enable = %d\n", enable);

	__smb135x_charging(chip, enable);

	if (chip->usb_psy) {
		pr_debug("usb psy changed\n");
		power_supply_changed(chip->usb_psy);
	}
	if (chip->dc_psy_type != -EINVAL) {
		pr_debug("dc psy changed\n");
		power_supply_changed(&chip->dc_psy);
	}
	pr_debug("charging %s\n",
			enable ?  "enabled" : "disabled running from batt");
	return rc;
}


//************************************************
// Таблицы и обработчики SYSFS-веток
//************************************************
#define LAST_CNFG_REG	0x1F
static int show_cnfg_regs(struct seq_file *m, void *data)
{
	struct smb135x_chg *chip = m->private;
	int rc;
	u8 reg;
	u8 addr;

	for (addr = 0; addr <= LAST_CNFG_REG; addr++) {
		rc = smb135x_read(chip, addr, &reg);
		if (!rc)
			seq_printf(m, "0x%02x = 0x%02x\n", addr, reg);
	}

	return 0;
}
static int cnfg_debugfs_open(struct inode *inode, struct file *file)
{
	struct smb135x_chg *chip = inode->i_private;

	return single_open(file, show_cnfg_regs, chip);
}

static const struct file_operations cnfg_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= cnfg_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#define FIRST_STATUS_REG	0x46
#define LAST_STATUS_REG		0x56
static int show_status_regs(struct seq_file *m, void *data)
{
	struct smb135x_chg *chip = m->private;
	int rc;
	u8 reg;
	u8 addr;

	for (addr = FIRST_STATUS_REG; addr <= LAST_STATUS_REG; addr++) {
		rc = smb135x_read(chip, addr, &reg);
		if (!rc)
			seq_printf(m, "0x%02x = 0x%02x\n", addr, reg);
	}

	return 0;
}

static int status_debugfs_open(struct inode *inode, struct file *file)
{
	struct smb135x_chg *chip = inode->i_private;

	return single_open(file, show_status_regs, chip);
}

static const struct file_operations status_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= status_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int show_irq_count(struct seq_file *m, void *data)
{
	int i, j, total = 0;

	for (i = 0; i < ARRAY_SIZE(handlers); i++)
		for (j = 0; j < 4; j++) {
			seq_printf(m, "%s=%d\t(high=%d low=%d)\n",
						handlers[i].irq_info[j].name,
						handlers[i].irq_info[j].high
						+ handlers[i].irq_info[j].low,
						handlers[i].irq_info[j].high,
						handlers[i].irq_info[j].low);
			total += (handlers[i].irq_info[j].high
					+ handlers[i].irq_info[j].low);
		}

	seq_printf(m, "\n\tTotal = %d\n", total);

	return 0;
}

static int irq_count_debugfs_open(struct inode *inode, struct file *file)
{
	struct smb135x_chg *chip = inode->i_private;

	return single_open(file, show_irq_count, chip);
}

static const struct file_operations irq_count_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= irq_count_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int get_reg(void *data, u64 *val)
{
	struct smb135x_chg *chip = data;
	int rc;
	u8 temp;

	rc = smb135x_read(chip, chip->peek_poke_address, &temp);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't read reg %x rc = %d\n",
			chip->peek_poke_address, rc);
		return -EAGAIN;
	}
	*val = temp;
	return 0;
}

static int set_reg(void *data, u64 val)
{
	struct smb135x_chg *chip = data;
	int rc;
	u8 temp;

	temp = (u8) val;
	rc = smb135x_write(chip, chip->peek_poke_address, temp);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't write 0x%02x to 0x%02x rc= %d\n",
			chip->peek_poke_address, temp, rc);
		return -EAGAIN;
	}
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(poke_poke_debug_ops, get_reg, set_reg, "0x%02llx\n");

static int force_irq_set(void *data, u64 val)
{
	struct smb135x_chg *chip = data;

	smb135x_chg_stat_handler(chip->client->irq, data);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_irq_ops, NULL, force_irq_set, "0x%02llx\n");

static int force_rechg_set(void *data, u64 val)
{
	int rc;
	struct smb135x_chg *chip = data;

	if (!chip->chg_enabled) {
		pr_debug("Charging Disabled force recharge not allowed\n");
		return -EINVAL;
	}

	rc = smb135x_masked_write(chip, CFG_14_REG, EN_CHG_INHIBIT_BIT, 0);
	if (rc)
		dev_err(chip->dev,
			"Couldn't disable charge-inhibit rc=%d\n", rc);
	/* delay for charge-inhibit to take affect */
	msleep(500);
	rc |= smb135x_charging(chip, false);
	rc |= smb135x_charging(chip, true);
	rc |= smb135x_masked_write(chip, CFG_14_REG, EN_CHG_INHIBIT_BIT,
						EN_CHG_INHIBIT_BIT);
	if (rc)
		dev_err(chip->dev,
			"Couldn't enable charge-inhibit rc=%d\n", rc);

	return rc;
}
DEFINE_SIMPLE_ATTRIBUTE(force_rechg_ops, NULL, force_rechg_set, "0x%02llx\n");

#define FIRST_CMD_REG	0x40
#define LAST_CMD_REG	0x42
static int show_cmd_regs(struct seq_file *m, void *data)
{
	struct smb135x_chg *chip = m->private;
	int rc;
	u8 reg;
	u8 addr;

	for (addr = FIRST_CMD_REG; addr <= LAST_CMD_REG; addr++) {
		rc = smb135x_read(chip, addr, &reg);
		if (!rc)
			seq_printf(m, "0x%02x = 0x%02x\n", addr, reg);
	}

	return 0;
}

static int cmd_debugfs_open(struct inode *inode, struct file *file)
{
	struct smb135x_chg *chip = inode->i_private;

	return single_open(file, show_cmd_regs, chip);
}

static const struct file_operations cmd_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= cmd_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


//************************************************
//* Разрешение записи в защищенные регистры
//************************************************
static int smb135x_enable_volatile_writes(struct smb135x_chg *chip)
{
	int rc;

	rc = smb135x_masked_write(chip, CMD_I2C_REG,
			ALLOW_VOLATILE_BIT, ALLOW_VOLATILE_BIT);
	if (rc < 0)
		dev_err(chip->dev,
			"Couldn't set VOLATILE_W_PERM_BIT rc=%d\n", rc);

	return rc;
}

//************************************************
//*  Хер знает что, типа беспроводной зарядки
//************************************************
static void wireless_insertion_work(struct work_struct *work) {

struct smb135x_chg *chip =container_of(work, struct smb135x_chg,wireless_insertion_work.work);

/* unsuspend dc */
smb135x_path_suspend(chip, DC, CURRENT, false);
}	
	
	
#define SMB1356_VERSION3_BIT	BIT(7)
#define SMB1357_VERSION1_VAL	0x01
#define SMB1358_VERSION1_VAL	0x02
#define SMB1359_VERSION1_VAL	0x00
#define SMB1357_VERSION2_VAL	0x01
#define SMB1358_VERSION2_VAL	0x02
#define SMB1359_VERSION2_VAL	0x00

//************************************************
//* Процедуры идентификации чипа
//************************************************
static int read_revision(struct smb135x_chg *chip, u8 *revision) {

int rc;
u8 reg;

rc = smb135x_read(chip, STATUS_9_REG, &reg);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't read status 9 rc = %d\n", rc);
	return rc;
}
*revision = (reg & REV_MASK);
return 0;
}

//************************************************
static int read_version1(struct smb135x_chg *chip, u8 *version) {

int rc;
u8 reg;

rc = smb135x_read(chip, VERSION1_REG, &reg);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't read version 1 rc = %d\n", rc);
	return rc;
}
*version = (reg & VERSION1_MASK) >> VERSION1_SHIFT;
return 0;
}

//************************************************
static int read_version2(struct smb135x_chg *chip, u8 *version) {
  
int rc;
u8 reg;

rc = smb135x_read(chip, VERSION2_REG, &reg);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't read version 2 rc = %d\n", rc);
	return rc;
}
*version = (reg & VERSION2_MASK);
return 0;
}

//************************************************
static int read_version3(struct smb135x_chg *chip, u8 *version) {
  
int rc;
u8 reg;

rc = smb135x_read(chip, VERSION3_REG, &reg);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't read version 3 rc = %d\n", rc);
	return rc;
}
*version = reg;
return 0;
}


//************************************************
//* Проверка на ошибку в реализации v1.0 и v1.1
//************************************************
#define TRIM_23_REG		0x23
#define CHECK_USB100_GOOD_BIT	BIT(1)
static bool is_usb100_broken(struct smb135x_chg *chip) {

int rc;
u8 reg;

rc = smb135x_read(chip, TRIM_23_REG, &reg);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't read status 9 rc = %d\n", rc);
	return rc;
}
return !!(reg & CHECK_USB100_GOOD_BIT);
}



//**********************************************************
//*  Определение версии и ревизии чипа - верхний уровень
//**********************************************************
static int smb135x_chip_version_and_revision(struct smb135x_chg *chip) {

int rc;
u8 version1, version2, version3;

/* read the revision */
rc = read_revision(chip, &chip->revision);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't read revision rc = %d\n", rc);
	return rc;
}

if (chip->revision >= REV_MAX || revision_str[chip->revision] == NULL) {
	dev_err(chip->dev, "Bad revision found = %d\n", chip->revision);
	return -EINVAL;
}

/* check if it is smb1356 */
rc = read_version3(chip, &version3);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't read version3 rc = %d\n", rc);
	return rc;
}

if (version3 & SMB1356_VERSION3_BIT) {
	chip->version = V_SMB1356;
	goto wrkarnd_and_input_current_values;
}

/* check if it is smb1357, smb1358 or smb1359 based on revision */
if (chip->revision <= REV_1_1) {
	rc = read_version1(chip, &version1);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't read version 1 rc = %d\n", rc);
		return rc;
	}
	switch (version1) {
	case SMB1357_VERSION1_VAL:
		chip->version = V_SMB1357;
		break;
	case SMB1358_VERSION1_VAL:
		chip->version = V_SMB1358;
		break;
	case SMB1359_VERSION1_VAL:
		chip->version = V_SMB1359;
		break;
	default:
		dev_err(chip->dev,
			"Unknown version 1 = 0x%02x rc = %d\n",
			version1, rc);
		return rc;
	}
} else {
	rc = read_version2(chip, &version2);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't read version 2 rc = %d\n", rc);
		return rc;
	}
	switch (version2) {
	case SMB1357_VERSION2_VAL:
		chip->version = V_SMB1357;
		break;
	case SMB1358_VERSION2_VAL:
		chip->version = V_SMB1358;
		break;
	case SMB1359_VERSION2_VAL:
		chip->version = V_SMB1359;
		break;
	default:
		dev_err(chip->dev,
				"Unknown version 2 = 0x%02x rc = %d\n",
				version2, rc);
		return rc;
	}
}

wrkarnd_and_input_current_values:

if (is_usb100_broken(chip))
	chip->workaround_flags |= WRKARND_USB100_BIT;
/*
 * Rev v1.0 and v1.1 of SMB135x fails charger type detection
 * (apsd) due to interference on the D+/- lines by the USB phy.
 * Set the workaround flag to disable charger type reporting
 * for this revision.
 */
if (chip->revision <= REV_1_1)
	chip->workaround_flags |= WRKARND_APSD_FAIL;

pr_debug("workaround_flags = %x\n", chip->workaround_flags);

switch (chip->version) {
case V_SMB1356:
	chip->usb_current_table = usb_current_table_smb1356;
	chip->usb_current_arr_size= ARRAY_SIZE(usb_current_table_smb1356);
	chip->dc_current_table = dc_current_table_smb1356;
	chip->dc_current_arr_size= ARRAY_SIZE(dc_current_table_smb1356);
	break;
case V_SMB1357:
	chip->usb_current_table = usb_current_table_smb1357_smb1358;
	chip->usb_current_arr_size= ARRAY_SIZE(usb_current_table_smb1357_smb1358);
	chip->dc_current_table = dc_current_table;
	chip->dc_current_arr_size = ARRAY_SIZE(dc_current_table);
	break;
case V_SMB1358:
	chip->usb_current_table = usb_current_table_smb1357_smb1358;
	chip->usb_current_arr_size= ARRAY_SIZE(usb_current_table_smb1357_smb1358);
	chip->dc_current_table = dc_current_table;
	chip->dc_current_arr_size = ARRAY_SIZE(dc_current_table);
	break;
case V_SMB1359:
	chip->usb_current_table = usb_current_table_smb1359;
	chip->usb_current_arr_size= ARRAY_SIZE(usb_current_table_smb1359);
	chip->dc_current_table = dc_current_table;
	chip->dc_current_arr_size = ARRAY_SIZE(dc_current_table);
	break;
	  
}

return 0;
}

//**************************************
//* Отладночный дамп регистров чипа
//**************************************
static void dump_regs(struct smb135x_chg *chip) {
	int rc;
	u8 reg;
	u8 addr;

	for (addr = 0; addr <= LAST_CNFG_REG; addr++) {
		rc = smb135x_read(chip, addr, &reg);
		if (rc < 0)
			dev_err(chip->dev, "Couldn't read 0x%02x rc = %d\n",
					addr, rc);
		else
			pr_debug("0x%02x = 0x%02x\n", addr, reg);
	}

	for (addr = FIRST_STATUS_REG; addr <= LAST_STATUS_REG; addr++) {
		rc = smb135x_read(chip, addr, &reg);
		if (rc < 0)
			dev_err(chip->dev, "Couldn't read 0x%02x rc = %d\n",
					addr, rc);
		else
			pr_debug("0x%02x = 0x%02x\n", addr, reg);
	}

	for (addr = FIRST_CMD_REG; addr <= LAST_CMD_REG; addr++) {
		rc = smb135x_read(chip, addr, &reg);
		if (rc < 0)
			dev_err(chip->dev, "Couldn't read 0x%02x rc = %d\n",
					addr, rc);
		else
			pr_debug("0x%02x = 0x%02x\n", addr, reg);
	}
}

//******************************************************************
//* Процедуры обратного вызова для работы с регулятором напряжения
//******************************************************************

static bool elapsed_msec_greater(struct timeval *start_time,
				struct timeval *end_time, int ms)
{
	int msec_elapsed;

	msec_elapsed = (end_time->tv_sec - start_time->tv_sec) * 1000 +
		DIV_ROUND_UP(end_time->tv_usec - start_time->tv_usec, 1000);

	return (msec_elapsed > ms);
}

#define MAX_STEP_MS		10
static int smb135x_chg_otg_regulator_enable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct smb135x_chg *chip = rdev_get_drvdata(rdev);
	int restart_count = 0;
	struct timeval time_a, time_b, time_c, time_d;

	/*
	 * Workaround for a hardware bug where the OTG needs to be enabled
	 * disabled and enabled for it to be actually enabled. The time between
	 * each step should be atmost MAX_STEP_MS
	 *
	 * Note that if enable-disable executes within the timeframe
	 * but the final enable takes more than MAX_STEP_ME, we treat it as
	 * the first enable and try disabling again. We don't want
	 * to issue enable back to back.
	 *
	 * Notice the instances when time is captured and the successive
	 * steps.
	 * timeA-enable-timeC-disable-timeB-enable-timeD.
	 * When
	 * (timeB - timeA) < MAX_STEP_MS AND (timeC - timeD) < MAX_STEP_MS
	 * then it is guaranteed that the successive steps
	 * must have executed within MAX_STEP_MS
	 */
	do_gettimeofday(&time_a);
restart_from_enable:
	/* first step - enable otg */
	rc = smb135x_masked_write(chip, CMD_CHG_REG, OTG_EN, OTG_EN);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't enable OTG mode rc=%d\n", rc);
		return rc;
	}

restart_from_disable:
	/* second step - disable otg */
	do_gettimeofday(&time_c);
	rc = smb135x_masked_write(chip, CMD_CHG_REG, OTG_EN, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't enable OTG mode rc=%d\n", rc);
		return rc;
	}
	do_gettimeofday(&time_b);

	if (elapsed_msec_greater(&time_a, &time_b, MAX_STEP_MS)) {
		restart_count++;
		if (restart_count > 10) {
			dev_err(chip->dev,
				"Couldn't enable OTG restart_count=%d\n",
				restart_count);
			return -EAGAIN;
		}
		time_a = time_b;
		pr_debug("restarting from first enable\n");
		goto restart_from_enable;
	}

	/* third step (first step in case of a failure) - enable otg */
	time_a = time_b;
	rc = smb135x_masked_write(chip, CMD_CHG_REG, OTG_EN, OTG_EN);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't enable OTG mode rc=%d\n", rc);
		return rc;
	}
	do_gettimeofday(&time_d);

	if (elapsed_msec_greater(&time_c, &time_d, MAX_STEP_MS)) {
		restart_count++;
		if (restart_count > 10) {
			dev_err(chip->dev,
				"Couldn't enable OTG restart_count=%d\n",
				restart_count);
			return -EAGAIN;
		}
		pr_debug("restarting from disable\n");
		goto restart_from_disable;
	}
	return rc;
}

static int smb135x_chg_otg_regulator_disable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct smb135x_chg *chip = rdev_get_drvdata(rdev);

	rc = smb135x_masked_write(chip, CMD_CHG_REG, OTG_EN, 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't disable OTG mode rc=%d\n", rc);
	return rc;
}

static int smb135x_chg_otg_regulator_is_enable(struct regulator_dev *rdev)
{
	int rc = 0;
	u8 reg = 0;
	struct smb135x_chg *chip = rdev_get_drvdata(rdev);

	rc = smb135x_read(chip, CMD_CHG_REG, &reg);
	if (rc < 0) {
		dev_err(chip->dev,
				"Couldn't read OTG enable bit rc=%d\n", rc);
		return rc;
	}

	return  (reg & OTG_EN) ? 1 : 0;
}

//***************************************************************************
//* Таблица процедур обратного вызова для работы с регулятором напряжения
//***************************************************************************
struct regulator_ops smb135x_chg_otg_reg_ops = {
	.enable		= smb135x_chg_otg_regulator_enable,
	.disable	= smb135x_chg_otg_regulator_disable,
	.is_enabled	= smb135x_chg_otg_regulator_is_enable,
};

/*
struct regulator_desc {
211  000  540     const char *name;
212  004  544     const char *supply_name;
213  008  548     int id;
214  012  552     bool continuous_voltage_range;
215  016  556    unsigned n_voltages;
216  020  560    struct regulator_ops *ops;
217  024  564    int irq;
218  028  568    enum regulator_type type;
219  032  572    struct module *owner;
*/

//**************************************
//* Настройка регулятора напряжения
//**************************************

static int smb135x_regulator_init(struct smb135x_chg *chip)
{
	int rc = 0;
	struct regulator_init_data *init_data;
	struct regulator_config cfg = {};

	init_data = of_get_regulator_init_data(chip->dev, chip->dev->of_node);
	if (!init_data) {
		dev_err(chip->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	if (init_data->constraints.name) {
		chip->otg_vreg.rdesc.owner = THIS_MODULE;
		chip->otg_vreg.rdesc.type = REGULATOR_VOLTAGE;
		chip->otg_vreg.rdesc.ops = &smb135x_chg_otg_reg_ops;
		chip->otg_vreg.rdesc.name = init_data->constraints.name;

		cfg.dev = chip->dev;
		cfg.init_data = init_data;
		cfg.driver_data = chip;
		cfg.of_node = chip->dev->of_node;

		init_data->constraints.valid_ops_mask
			|= REGULATOR_CHANGE_STATUS;

		chip->otg_vreg.rdev = regulator_register(
						&chip->otg_vreg.rdesc, &cfg);
		if (IS_ERR(chip->otg_vreg.rdev)) {
			rc = PTR_ERR(chip->otg_vreg.rdev);
			chip->otg_vreg.rdev = NULL;
			if (rc != -EPROBE_DEFER)
				dev_err(chip->dev,
					"OTG reg failed, rc=%d\n", rc);
		}
	}

	return rc;
}

//**************************************
//* Отключение регулятора напряжения
//**************************************

static void smb135x_regulator_deinit(struct smb135x_chg *chip)
{
	if (chip->otg_vreg.rdev)
		regulator_unregister(chip->otg_vreg.rdev);
}






//*******************************************************
//* Процедуры определения текущего состояния чипа
//*******************************************************
static int determine_initial_status(struct smb135x_chg *chip) {
	int rc;
	u8 reg;

	/*
	 * It is okay to read the interrupt status here since
	 * interrupts aren't requested. reading interrupt status
	 * clears the interrupt so be careful to read interrupt
	 * status only in interrupt handling code
	 */

	chip->batt_present = true;
	rc = smb135x_read(chip, IRQ_B_REG, &reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read irq b rc = %d\n", rc);
		return rc;
	}
	if (reg & IRQ_B_BATT_TERMINAL_BIT || reg & IRQ_B_BATT_MISSING_BIT)
		chip->batt_present = false;
	rc = smb135x_read(chip, STATUS_4_REG, &reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read status 4 rc = %d\n", rc);
		return rc;
	}
	/* treat battery gone if less than 2V */
	if (reg & BATT_LESS_THAN_2V)
		chip->batt_present = false;

	rc = smb135x_read(chip, IRQ_A_REG, &reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read irq A rc = %d\n", rc);
		return rc;
	}

	if (reg & IRQ_A_HOT_HARD_BIT)
		chip->batt_hot = true;
	if (reg & IRQ_A_COLD_HARD_BIT)
		chip->batt_cold = true;
	if (reg & IRQ_A_HOT_SOFT_BIT)
		chip->batt_warm = true;
	if (reg & IRQ_A_COLD_SOFT_BIT)
		chip->batt_cool = true;

	rc = smb135x_read(chip, IRQ_C_REG, &reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read irq A rc = %d\n", rc);
		return rc;
	}
	if (reg & IRQ_C_TERM_BIT)
		chip->chg_done_batt_full = true;

	rc = smb135x_read(chip, IRQ_E_REG, &reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read irq E rc = %d\n", rc);
		return rc;
	}
	chip->usb_present = !(reg & IRQ_E_USB_OV_BIT)
				&& !(reg & IRQ_E_USB_UV_BIT);
	chip->dc_present = !(reg & IRQ_E_DC_OV_BIT) && !(reg & IRQ_E_DC_UV_BIT);

	if (chip->usb_present)
		handle_usb_insertion(chip);
	else
		handle_usb_removal(chip);

	if (chip->dc_psy_type != -EINVAL) {
		if (chip->dc_psy_type == POWER_SUPPLY_TYPE_WIRELESS) {
			/*
			 * put the dc path in suspend state if it is powered
			 * by wireless charger
			 */
			if (chip->dc_present)
				smb135x_path_suspend(chip, DC, CURRENT, false);
			else
				smb135x_path_suspend(chip, DC, CURRENT, true);
		}
	}

	chip->usb_slave_present = is_usb_slave_present(chip);
	if (chip->usb_psy) {
		pr_debug("setting usb psy usb_otg = %d\n",
				chip->usb_slave_present);
		power_supply_set_usb_otg(chip->usb_psy,
			chip->usb_slave_present);
	}
	return 0;
}

#define MIN_FLOAT_MV	3600
#define MAX_FLOAT_MV	4500

#define MID_RANGE_FLOAT_MV_MIN		3600
#define MID_RANGE_FLOAT_MIN_VAL		0x05
#define MID_RANGE_FLOAT_STEP_MV		20

#define HIGH_RANGE_FLOAT_MIN_MV		4340
#define HIGH_RANGE_FLOAT_MIN_VAL	0x2A
#define HIGH_RANGE_FLOAT_STEP_MV	10

#define VHIGH_RANGE_FLOAT_MIN_MV	4400
#define VHIGH_RANGE_FLOAT_MIN_VAL	0x2E
#define VHIGH_RANGE_FLOAT_STEP_MV	20

//***************************************************************
//*
//****************************************************************
static int smb135x_float_voltage_set(struct smb135x_chg *chip, int vfloat_mv) {
	u8 temp;

	if ((vfloat_mv < MIN_FLOAT_MV) || (vfloat_mv > MAX_FLOAT_MV)) {
		dev_err(chip->dev, "bad float voltage mv =%d asked to set\n",
					vfloat_mv);
		return -EINVAL;
	}

	if (vfloat_mv <= HIGH_RANGE_FLOAT_MIN_MV) {
		/* mid range */
		temp = MID_RANGE_FLOAT_MIN_VAL
			+ (vfloat_mv - MID_RANGE_FLOAT_MV_MIN)
				/ MID_RANGE_FLOAT_STEP_MV;
	} else if (vfloat_mv <= VHIGH_RANGE_FLOAT_MIN_MV) {
		/* high range */
		temp = HIGH_RANGE_FLOAT_MIN_VAL
			+ (vfloat_mv - HIGH_RANGE_FLOAT_MIN_MV)
				/ HIGH_RANGE_FLOAT_STEP_MV;
	} else {
		/* very high range */
		temp = VHIGH_RANGE_FLOAT_MIN_VAL
			+ (vfloat_mv - VHIGH_RANGE_FLOAT_MIN_MV)
				/ VHIGH_RANGE_FLOAT_STEP_MV;
	}

	return smb135x_write(chip, VFLOAT_REG, temp);
}

//**************************************
//*
//**************************************
static int smb135x_set_dc_chg_current(struct smb135x_chg *chip,
							int current_ma)
{
	int i, rc;
	u8 dc_cur_val;

	for (i = chip->dc_current_arr_size - 1; i >= 0; i--) {
		if (chip->dc_psy_ma >= chip->dc_current_table[i])
			break;
	}
	dc_cur_val = i & DCIN_INPUT_MASK;
	rc = smb135x_masked_write(chip, CFG_10_REG,
				DCIN_INPUT_MASK, dc_cur_val);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set dc charge current rc = %d\n",
				rc);
		return rc;
	}
	return 0;
}

//**************************************
//* Настройка аппаратных компонентов
//**************************************
static int smb135x_hw_init(struct smb135x_chg *chip) {
int rc;
int i;
u8 reg, mask;

if (chip->therm_bias_vreg) {
	rc = regulator_enable(chip->therm_bias_vreg);
	if (rc) {
		pr_err("Couldn't enable therm-bias rc = %d\n", rc);
		return rc;
	}
}

rc = smb135x_enable_volatile_writes(chip);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't configure for volatile rc = %d\n",
			rc);
	return rc;
}

/*
 * force using current from the register i.e. ignore auto
 * power source detect (APSD) mA ratings
 */
mask = USE_REGISTER_FOR_CURRENT;

if (chip->workaround_flags & WRKARND_USB100_BIT)
	reg = 0;
else
	/* this ignores APSD results */
	reg = USE_REGISTER_FOR_CURRENT;

rc = smb135x_masked_write(chip, CMD_INPUT_LIMIT, mask, reg);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't set input limit cmd rc=%d\n", rc);
	return rc;
}

/* set bit 0 = 100mA bit 1 = 500mA and set register control */
rc = smb135x_masked_write(chip, CFG_E_REG,
		POLARITY_100_500_BIT | USB_CTRL_BY_PIN_BIT,
		POLARITY_100_500_BIT);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't set usbin cfg rc=%d\n", rc);
	return rc;
}

/*
 * set chg en by cmd register, set chg en by writing bit 1,
 * enable auto pre to fast, enable current termination, enable
 * auto recharge, enable chg inhibition
 */
rc = smb135x_masked_write(chip, CFG_14_REG,
		CHG_EN_BY_PIN_BIT | CHG_EN_ACTIVE_LOW_BIT
		| PRE_TO_FAST_REQ_CMD_BIT | DISABLE_AUTO_RECHARGE_BIT
		| EN_CHG_INHIBIT_BIT, EN_CHG_INHIBIT_BIT);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't set cfg 14 rc=%d\n", rc);
	return rc;
}

/* control USB suspend via command bits */
rc = smb135x_masked_write(chip, USBIN_DCIN_CFG_REG,
	USBIN_SUSPEND_VIA_COMMAND_BIT, USBIN_SUSPEND_VIA_COMMAND_BIT);

/* set the float voltage */
if (chip->vfloat_mv != -EINVAL) {
	rc = smb135x_float_voltage_set(chip, chip->vfloat_mv);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't set float voltage rc = %d\n", rc);
		return rc;
	}
}

/* set iterm */
if (chip->iterm_ma != -EINVAL) {
	if (chip->iterm_disabled) {
		dev_err(chip->dev, "Error: Both iterm_disabled and iterm_ma set\n");
		return -EINVAL;
	} else {
		if (chip->iterm_ma <= 50)
			reg = CHG_ITERM_50MA;
		else if (chip->iterm_ma <= 100)
			reg = CHG_ITERM_100MA;
		else if (chip->iterm_ma <= 150)
			reg = CHG_ITERM_150MA;
		else if (chip->iterm_ma <= 200)
			reg = CHG_ITERM_200MA;
		else if (chip->iterm_ma <= 250)
			reg = CHG_ITERM_250MA;
		else if (chip->iterm_ma <= 300)
			reg = CHG_ITERM_300MA;
		else if (chip->iterm_ma <= 500)
			reg = CHG_ITERM_500MA;
		else
			reg = CHG_ITERM_600MA;

		rc = smb135x_masked_write(chip, CFG_3_REG,
						CHG_ITERM_MASK, reg);
		if (rc) {
			dev_err(chip->dev,
				"Couldn't set iterm rc = %d\n", rc);
			return rc;
		}

		rc = smb135x_masked_write(chip, CFG_14_REG,
					DISABLE_CURRENT_TERM_BIT, 0);
		if (rc) {
			dev_err(chip->dev,
				"Couldn't enable iterm rc = %d\n", rc);
			return rc;
		}
	}
} else  if (chip->iterm_disabled) {
	rc = smb135x_masked_write(chip, CFG_14_REG,
				DISABLE_CURRENT_TERM_BIT,
				DISABLE_CURRENT_TERM_BIT);
	if (rc) {
		dev_err(chip->dev, "Couldn't set iterm rc = %d\n",
							rc);
		return rc;
	}
}

/* set the safety time voltage */
if (chip->safety_time != -EINVAL) {
	if (chip->safety_time == 0) {
		/* safety timer disabled */
		rc = smb135x_masked_write(chip, CFG_16_REG,
						SAFETY_TIME_EN_BIT, 0);
		if (rc < 0) {
			dev_err(chip->dev,
			"Couldn't disable safety timer rc = %d\n",
			rc);
			return rc;
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(chg_time); i++) {
			if (chip->safety_time <= chg_time[i]) {
				reg = i << SAFETY_TIME_MINUTES_SHIFT;
				break;
			}
		}
		rc = smb135x_masked_write(chip, CFG_16_REG,
			SAFETY_TIME_EN_BIT | SAFETY_TIME_MINUTES_MASK,
			SAFETY_TIME_EN_BIT | reg);
		if (rc < 0) {
			dev_err(chip->dev,
				"Couldn't set safety timer rc = %d\n",
				rc);
			return rc;
		}
	}
}

/* battery missing detection */
rc = smb135x_masked_write(chip, CFG_19_REG,
		BATT_MISSING_ALGO_BIT | BATT_MISSING_THERM_BIT,
		chip->bmd_algo_disabled ? BATT_MISSING_THERM_BIT :
					BATT_MISSING_ALGO_BIT);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't set batt_missing config = %d\n",
								rc);
	return rc;
}

__smb135x_charging(chip, chip->chg_enabled);

/* interrupt enabling - active low */
if (chip->client->irq) {
	mask = CHG_STAT_IRQ_ONLY_BIT | CHG_STAT_ACTIVE_HIGH_BIT
		| CHG_STAT_DISABLE_BIT;
	reg = CHG_STAT_IRQ_ONLY_BIT;
	rc = smb135x_masked_write(chip, CFG_17_REG, mask, reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set irq config rc = %d\n",
				rc);
		return rc;
	}

	/* enabling only interesting interrupts */
	rc = smb135x_write(chip, IRQ_CFG_REG,
		IRQ_BAT_HOT_COLD_HARD_BIT
		| IRQ_BAT_HOT_COLD_SOFT_BIT
		| IRQ_INTERNAL_TEMPERATURE_BIT
		| IRQ_USBIN_UV_BIT);

	rc |= smb135x_write(chip, IRQ2_CFG_REG,
		IRQ2_SAFETY_TIMER_BIT
		| IRQ2_CHG_ERR_BIT
		| IRQ2_CHG_PHASE_CHANGE_BIT
		| IRQ2_POWER_OK_BIT
		| IRQ2_BATT_MISSING_BIT
		| IRQ2_VBAT_LOW_BIT);

	rc |= smb135x_write(chip, IRQ3_CFG_REG, IRQ3_SRC_DETECT_BIT
			| IRQ3_DCIN_UV_BIT | IRQ3_RID_DETECT_BIT);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set irq enable rc = %d\n",
				rc);
		return rc;
	}
}

/* resume threshold */
if (chip->resume_delta_mv != -EINVAL) {
	if (chip->resume_delta_mv < 100)
		reg = CHG_INHIBIT_50MV_VAL;
	else if (chip->resume_delta_mv < 200)
		reg = CHG_INHIBIT_100MV_VAL;
	else if (chip->resume_delta_mv < 300)
		reg = CHG_INHIBIT_200MV_VAL;
	else
		reg = CHG_INHIBIT_300MV_VAL;

	rc = smb135x_masked_write(chip, CFG_4_REG,
					CHG_INHIBIT_MASK, reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set inhibit val rc = %d\n",
				rc);
		return rc;
	}

	if (chip->resume_delta_mv < 200)
		reg = 0;
	else
		 reg = RECHARGE_200MV_BIT;

	rc = smb135x_masked_write(chip, CFG_5_REG,
					RECHARGE_200MV_BIT, reg);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set recharge  rc = %d\n",
				rc);
		return rc;
	}
}

/* DC path current settings */
if (chip->dc_psy_type != -EINVAL) {
	rc = smb135x_set_dc_chg_current(chip, chip->dc_psy_ma);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set dc charge current rc = %d\n",
				rc);
		return rc;
	}
}

/*
 * on some devices the battery is powered via external sources which
 * could raise its voltage above the float voltage. smb135x chips go
 * in to reverse boost in such a situation and the workaround is to
 * disable float voltage compensation (note that the battery will appear
 * hot/cold when powered via external source).
 */

if (chip->soft_vfloat_comp_disabled) {
	mask = HOT_SOFT_VFLOAT_COMP_EN_BIT
			| COLD_SOFT_VFLOAT_COMP_EN_BIT;
	rc = smb135x_masked_write(chip, CFG_1A_REG, mask, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't disable soft vfloat rc = %d\n",
				rc);
		return rc;
	}
}

/*
 * Command mode for OTG control. This gives us RID interrupts but keeps
 * enabling the 5V OTG via i2c register control
 */
rc = smb135x_masked_write(chip, USBIN_OTG_REG, OTG_CNFG_MASK,
		OTG_CNFG_COMMAND_CTRL);
if (rc < 0) {
	dev_err(chip->dev, "Couldn't write to otg cfg reg rc = %d\n",
			rc);
	return rc;
}

return rc;
}

//**************************************
//*
//**************************************
static int smb135x_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smb135x_chg *chip = i2c_get_clientdata(client);
	int i, rc;

	/* Save the current IRQ config */
	for (i = 0; i < 3; i++) {
		rc = smb135x_read(chip, IRQ_CFG_REG + i,
					&chip->irq_cfg_mask[i]);
		if (rc)
			dev_err(chip->dev,
				"Couldn't save irq cfg regs rc=%d\n", rc);
	}

	/* enable only important IRQs */
	rc = smb135x_write(chip, IRQ_CFG_REG, IRQ_USBIN_UV_BIT);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set irq_cfg rc = %d\n", rc);

	rc = smb135x_write(chip, IRQ2_CFG_REG, IRQ2_BATT_MISSING_BIT
						| IRQ2_VBAT_LOW_BIT
						| IRQ2_POWER_OK_BIT);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set irq2_cfg rc = %d\n", rc);

	rc = smb135x_write(chip, IRQ3_CFG_REG, IRQ3_SRC_DETECT_BIT
			| IRQ3_DCIN_UV_BIT | IRQ3_RID_DETECT_BIT);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set irq3_cfg rc = %d\n", rc);

	mutex_lock(&chip->irq_complete);
	chip->resume_completed = false;
	mutex_unlock(&chip->irq_complete);

	return 0;
}

//**************************************
//*
//**************************************
static int smb135x_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smb135x_chg *chip = i2c_get_clientdata(client);

	if (chip->irq_waiting) {
		pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n");
		return -EBUSY;
	}
	return 0;
}

//**************************************
//*
//**************************************
static int smb135x_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smb135x_chg *chip = i2c_get_clientdata(client);
	int i, rc;

	/* Restore the IRQ config */
	for (i = 0; i < 3; i++) {
		rc = smb135x_write(chip, IRQ_CFG_REG + i,
					chip->irq_cfg_mask[i]);
		if (rc)
			dev_err(chip->dev,
				"Couldn't restore irq cfg regs rc=%d\n", rc);
	}
	mutex_lock(&chip->irq_complete);
	chip->resume_completed = true;
	if (chip->irq_waiting) {
		mutex_unlock(&chip->irq_complete);
		smb135x_chg_stat_handler(client->irq, chip);
		enable_irq(client->irq);
	} else {
		mutex_unlock(&chip->irq_complete);
	}
	return 0;
}


//**************************************
//*  Проверка наличия батарейки
//**************************************
static int smb135x_get_prop_batt_present(struct smb135x_chg *chip)
{
	int rc;
	u8 reg;

	rc = smb135x_read(chip, STATUS_4_REG, &reg);
	if (rc < 0)
		return 0;

	/* treat battery gone if less than 2V */
	if (reg & BATT_LESS_THAN_2V)
		return 0;

	return chip->batt_present;
}


//**************************************
//* Установка тока зарядки
//**************************************
int smb135x_set_current_limit(void *self, int mA) {

struct smb135x_chg* chip=self; 
int rc;  
int usb_supply_type;
int therm_ma,current_ma;

if ((chip == 0) || (mA<0)) {
    dev_err(&client->dev, "Error parameters in smb135x_set_current_limit\n");
    return -EINVAL;
}
if (chip->usb_psy == 0) return 1;
usb_supply_type=power_supply_get_supply_type(chip->usb_psy);
pr_info("[Core]get power_supply_type = %d",usb_supply_type);
if (usb_supply_type == 0) return 1;
pr_info("[Core]Set current limit = %d, usb_psy_ma = %d\n",mA,chip->usb_psy_ma);
chip->usb_psy_ma=mA;

rc=smb135x_get_prop_batt_present(chip);
if (chip->batt_present == 0) {
  pr_info("ignoring current request since battery is absent\n"
  return -EPERM;
}  
if ((chip->therm_lvl_sel != 0) && (chip->therm_lvl_sel < (chip->thermal_levels-1)))
    therm_ma=chip->thermal_mitigation[chip->therm_lvl_sel];
else therm_ma=chip->usb_psy_ma;  
current_ma = min(therm_ma, chip->usb_psy_ma);
rc=smb135x_set_usb_chg_current(chip,current_ma);
if (rc<0) {
  dev_err(&client->dev,"Couldn't set USB current to min(%d, %d) rc = %d\n",therm_ma,chip->usb_psy_ma,rc);
  return -EPERM;
}
return 1;
}
  
	  
#define DC_MA_MIN 300
#define DC_MA_MAX 2000

//**************************************
//*  Конструктор модуля
//**************************************
int smb135x_charger_probe(struct i2c_client* client, const struct i2c_device_id *id) {
  
int rc;  
struct power_supply* usb_psy;
struct smb135x_chg* chip;
unsigned char reg=0;
struct device_node* node; 
struct dentry *ent;
const struct of_device_id *match;
const char *dc_psy_type;

usb_psy = power_supply_get_by_name("usb");
if (!usb_psy) {
  dev_dbg(&client->dev, "USB supply not found; defer probe\n");
  return -EPROBE_DEFER;
}

chip = devm_kzalloc(&client->dev, sizeof(struct smb135x_chg), GFP_KERNEL);
if (!chip) {
  dev_err(&client->dev, "Unable to allocate memory\n");
  return -ENOMEM;
}

chip->client=client;
chip->usb_psy = usb_psy;
chip->dev = &client->dev;

INIT_DELAYED_WORK(&chip->wireless_insertion_work,wireless_insertion_work);
chip->fake_battery_soc = -EINVAL;

// мутексы
mutex_init(&chip->path_suspend_lock);
mutex_init(&chip->current_change_lock);
mutex_init(&chip->read_write_lock);

// детектим чип - читаем его регистр CFG4
rc = smb135x_read(chip, CFG_4_REG, &reg);
if (rc != 0) {
  pr_err("Failed to detect SMB135x, device may be absent\n");
  return -ENODEV;
}

// Разбор дерева параметров
node = chip->dev->of_node;
if (!node) {
	dev_err(chip->dev, "device tree info. missing\n");
	return -ENODEV;
}

match = of_match_node(smb135x_match_table, node);
if (match == NULL) {
	dev_err(chip->dev, "device tree match not found\n");
	return -ENODEV;
}

chip->usb_current_arr_size = (int)match->data;

rc = of_property_read_u32(node, "qcom,float-voltage-mv",&chip->vfloat_mv);
if (rc < 0)  chip->vfloat_mv = -EINVAL;

rc = of_property_read_u32(node, "qcom,charging-timeout",&chip->safety_time);
if (rc < 0) chip->safety_time = -EINVAL;

if (!rc && (chip->safety_time > chg_time[ARRAY_SIZE(chg_time) - 1])) {
	dev_err(chip->dev, "Bad charging-timeout %d\n",	chip->safety_time);
	return -EINVAL;
	}

chip->bmd_algo_disabled = of_property_read_bool(node,"qcom,bmd-algo-disabled");
chip->dc_psy_type = -EINVAL;
dc_psy_type = of_get_property(node, "qcom,dc-psy-type", NULL);
if (dc_psy_type) {
	if (strcmp(dc_psy_type, "Mains") == 0) 	chip->dc_psy_type = POWER_SUPPLY_TYPE_MAINS;
	else if (strcmp(dc_psy_type, "Wireless") == 0) 	chip->dc_psy_type = POWER_SUPPLY_TYPE_WIRELESS;
}

if (chip->dc_psy_type != -EINVAL) {
	rc = of_property_read_u32(node, "qcom,dc-psy-ma",&chip->dc_psy_ma);
	if (rc < 0) {
		dev_err(chip->dev,"no mA current for dc rc = %d\n", rc);
		return rc;
	}

	if ((chip->dc_psy_ma < DC_MA_MIN) || (chip->dc_psy_ma > DC_MA_MAX)) {
		dev_err(chip->dev, "Bad dc mA %d\n", chip->dc_psy_ma);
		return -EINVAL;
	}
}

rc = of_property_read_u32(node, "qcom,recharge-thresh-mv",&chip->resume_delta_mv);
if (rc < 0) chip->resume_delta_mv = -EINVAL;

rc = of_property_read_u32(node, "qcom,iterm-ma", &chip->iterm_ma);
if (rc < 0) chip->iterm_ma = -EINVAL;

chip->iterm_disabled = of_property_read_bool(node,"qcom,iterm-disabled");
chip->chg_enabled = !(of_property_read_bool(node,"qcom,charging-disabled"));
rc = of_property_read_string(node, "qcom,bms-psy-name",	&chip->bms_psy_name);
chip->soft_vfloat_comp_disabled = of_property_read_bool(node,"qcom,soft-vfloat-comp-disabled");

if (of_find_property(node, "therm-bias-supply", NULL)) {
  /* get the thermistor bias regulator */
  chip->therm_bias_vreg = devm_regulator_get(chip->dev,	"therm-bias");
  if (IS_ERR(chip->therm_bias_vreg)) return PTR_ERR(chip->therm_bias_vreg);
}

if (of_find_property(node, "qcom,thermal-mitigation",&chip->thermal_levels)) {
   chip->thermal_mitigation = devm_kzalloc(chip->dev,chip->thermal_levels,GFP_KERNEL);
   if (chip->thermal_mitigation == NULL) {
	pr_err("thermal mitigation kzalloc() failed.\n");
	return -ENOMEM;
   }
   chip->thermal_levels /= sizeof(int);
   rc = of_property_read_u32_array(node,"qcom,thermal-mitigation",chip->thermal_mitigation, chip->thermal_levels);
   if (rc) {
	pr_err("Couldn't read threm limits rc = %d\n", rc);
	return rc;
   }
}

chip->temp_monitor_disabled = of_property_read_bool(node,"qcom,temp-monitor-disabled");
  
i2c_set_clientdata(client, chip);

rc = smb135x_chip_version_and_revision(chip);
if (rc) {
  dev_err(&client->dev,
  "Couldn't detect version/revision rc=%d\n", rc);
  return rc;
}
dump_regs(chip);

rc = smb135x_regulator_init(chip);
if  (rc) {
	dev_err(&client->dev,"Couldn't initialize regulator rc=%d\n", rc);
	return rc;
}

rc = smb135x_hw_init(chip);
if (rc < 0) {
	dev_err(&client->dev,"Unable to intialize hardware rc = %d\n", rc);
	goto free_regulator;
}

rc = determine_initial_status(chip);
if (rc < 0) {
	dev_err(&client->dev,"Unable to determine init status rc = %d\n", rc);
	goto free_regulator;
}

chip->self=chip;
chip->set_current_limit_fn=smb135x_set_current_limit;
chip->enable_charge_fn=smb135x_enable_charge;
chip->set_otg_mode_fn=smb135x_set_otg_mode(void *self, int enable);
chip->ext_name_battery="battery";
chip->ext_name_usb="usb";

rc=charger_core_register(&client->dev,&chip->api);
if (rc != 0) {
  dev_err(&client->dev,"failed to register charger core, rc=%d\n",rc);
  goto free_regulator;
}

// Настройка прерываний
chip->resume_completed = true;
mutex_init(&chip->irq_complete);

if (client->irq) {
	rc = devm_request_threaded_irq(&client->dev, client->irq, NULL,	smb135x_chg_stat_handler,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT,"smb135x_chg_stat_irq", chip);
	if (rc < 0) {
	 dev_err(&client->dev,"request_irq for irq=%d  failed rc = %d\n",client->irq, rc);
	 goto unregister_dc_psy;
	}
	enable_irq_wake(client->irq);
}

// Создание sysfs-ветки

chip->debug_root = debugfs_create_dir("smb135x", NULL);
if (!chip->debug_root)	dev_err(chip->dev, "Couldn't create debug dir\n");
else  {

	ent = debugfs_create_file("config_registers", S_IFREG | S_IRUGO,chip->debug_root, chip,&cnfg_debugfs_ops);
	if (!ent) dev_err(chip->dev,"Couldn't create cnfg debug file rc = %d\n",rc);

	ent = debugfs_create_file("status_registers", S_IFREG | S_IRUGO,chip->debug_root, chip,&status_debugfs_ops);
	if (!ent) dev_err(chip->dev,"Couldn't create status debug file rc = %d\n",rc);

	ent = debugfs_create_file("cmd_registers", S_IFREG | S_IRUGO,chip->debug_root, chip,&cmd_debugfs_ops);
	if (!ent) dev_err(chip->dev,"Couldn't create cmd debug file rc = %d\n",rc);

	ent = debugfs_create_x32("address", S_IFREG | S_IWUSR | S_IRUGO,chip->debug_root,&(chip->peek_poke_address));
	if (!ent) dev_err(chip->dev,"Couldn't create address debug file rc = %d\n",rc);

	ent = debugfs_create_file("data", S_IFREG | S_IWUSR | S_IRUGO,chip->debug_root, chip,&poke_poke_debug_ops);
	if (!ent) dev_err(chip->dev,"Couldn't create data debug file rc = %d\n",rc);

	ent = debugfs_create_file("force_irq",S_IFREG | S_IWUSR | S_IRUGO,chip->debug_root, chip,&force_irq_ops);
	if (!ent) dev_err(chip->dev,"Couldn't create data debug file rc = %d\n",rc);

	ent = debugfs_create_x32("skip_writes",S_IFREG | S_IWUSR | S_IRUGO,chip->debug_root,&(chip->skip_writes));
	if (!ent) dev_err(chip->dev,"Couldn't create data debug file rc = %d\n",rc);

	ent = debugfs_create_x32("skip_reads", S_IFREG | S_IWUSR | S_IRUGO,chip->debug_root,&(chip->skip_reads));
	if (!ent) dev_err(chip->dev,"Couldn't create data debug file rc = %d\n",rc);

	ent = debugfs_create_file("irq_count", S_IFREG | S_IRUGO,chip->debug_root, chip,&irq_count_debugfs_ops);
	if (!ent) dev_err(chip->dev,"Couldn't create count debug file rc = %d\n",rc);

	ent = debugfs_create_file("force_recharge",S_IFREG | S_IWUSR | S_IRUGO,chip->debug_root, chip,&force_rechg_ops);
	if (!ent) dev_err(chip->dev,"Couldn't create recharge debug file rc = %d\n",rc);

	ent = debugfs_create_x32("usb_suspend_votes",S_IFREG | S_IWUSR | S_IRUGO,chip->debug_root,&(chip->usb_suspended));
	if (!ent) dev_err(chip->dev,"Couldn't create usb vote file rc = %d\n",rc);

	ent = debugfs_create_x32("dc_suspend_votes",S_IFREG | S_IWUSR | S_IRUGO,chip->debug_root,&(chip->dc_suspended));
	if (!ent) dev_err(chip->dev,"Couldn't create dc vote file rc = %d\n",rc);
}

dev_info(chip->dev, "SMB135X version = %s revision = %s successfully probed batt=%d dc = %d usb = %d\n",
	version_str[chip->version],revision_str[chip->revision],smb135x_get_prop_batt_present(chip),chip->dc_present, chip->usb_present);
// Все, закончили конструктор
return 0;

// выходы по ошикам
unregister_dc_psy:
free_regulator:

smb135x_regulator_deinit(chip);
return rc;
}

//**************************************
//* Деструктор модуля
//**************************************
static int smb135x_charger_remove(struct i2c_client *client) {
	int rc;
	struct smb135x_chg *chip = i2c_get_clientdata(client);

	if (chip->therm_bias_vreg) {
		rc = regulator_disable(chip->therm_bias_vreg);
		if (rc)
			pr_err("Couldn't disable therm-bias rc = %d\n", rc);
	}

	debugfs_remove_recursive(chip->debug_root);

	if (chip->dc_psy_type != -EINVAL)

	mutex_destroy(&chip->irq_complete);

	smb135x_regulator_deinit(chip);

	return 0;
}


//**************************************
//*  Структуры данных описания модуля
//**************************************

static struct of_device_id smb135x_match_table[] = {
	{ .compatible = "qcom,smb1356-charger", },
	{ .compatible = "qcom,smb1357-charger", },
	{ .compatible = "qcom,smb1358-charger", },
	{ .compatible = "qcom,smb1359-charger", },
	{ },
};

static const struct dev_pm_ops smb135x_pm_ops = {
	.resume		= smb135x_resume,
	.suspend_noirq	= smb135x_suspend_noirq,
	.suspend	= smb135x_suspend,
};

static const struct i2c_device_id smb135x_charger_id[] = {
	{"smb135x-charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, smb135x_charger_id);

static struct i2c_driver smb135x_charger_driver = {
	.driver		= {
		.name		= "smb135x-charger",
		.owner		= THIS_MODULE,
		.of_match_table	= smb135x_match_table,
		.pm		= &smb135x_pm_ops,
	},
	.probe		= smb135x_charger_probe,
	.remove		= smb135x_charger_remove,
	.id_table	= smb135x_charger_id,
};

module_i2c_driver(smb135x_charger_driver);

MODULE_DESCRIPTION("SMB135x Charger");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:smb135x-charger");
