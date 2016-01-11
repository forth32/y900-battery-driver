#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/mfd/88pm860x.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/qpnp/qpnp-adc.h>
#include "battery_core.h"
#include "charger_core.h"

// макросы для формирования битовых масок
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

  struct smb135x_chg* self;        // 92
  (*set_current_limit_fn)(void *self, int mA);  //96
  
  u8	revision;    // 176
  int	version;     // 180
  bool	chg_enabled; // 184
  bool	usb_present; // 185
  bool	dc_present;  // 186
  bool	usb_slave_present;  //187
  
  bool	bmd_algo_disabled;  // 189
  bool	iterm_disabled;     // 190
  int	iterm_ma;        // 192
  int	vfloat_mv;       // 196
  int	safety_time;     // 200
  int resume_delta_mv;   // 204
  int fake_battery_soc;  // 208
  int cx212;     
  int usb_current_arr_size;  // 216
  int* usb_current_table;    // 220
  int	dc_current_arr_size;  // 224
  int	*dc_current_table;    // 228

  struct power_supply* usb_psy;    //236

  int	dc_psy_type;  // 464
  int	dc_psy_ma;    // 468
  const char* bms_psy_name; // 472

  bool	chg_done_batt_full;  // 476
  bool	batt_present;    // 477
  bool	batt_hot;  // 478
  bool	batt_cold; //479
  bool	batt_warm; //480
  bool	batt_cool; //481
  bool temp_monitor_disabled; // 482
  
  struct mutex	path_suspend_lock;  // 496, 40 байт

  struct smb135x_regulator  otg_vreg; //540
   
  u32   workaround_flags;          // 648
  bool	soft_vfloat_comp_disabled; // 652

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

static int smb135x_path_suspend(struct smb135x_chg *chip, enum path_type path,
						int reason, bool suspend)
{
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

static int smb135x_charging(struct smb135x_chg *chip, int enable)
{
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
//*  Конструктор модуля
//**************************************
int smb135x_charger_probe(i2c_client *client, const i2c_device_id *id) {
  
int rc;  
struct power_supply* usb_psy;
struct smb135x_chg* chip;
unsigned char reg=0;
struct device_node*; 

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
