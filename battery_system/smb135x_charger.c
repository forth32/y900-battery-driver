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

//**************************************
//*  Главная интерфейсная структура 
//**************************************
// размер 828 байт

struct smb135x_chg {


  struct delayed_work work;  // 700, размер 76 
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
   // int cpu; 776

  
};  
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

	/* psy */
	struct power_supply		*usb_psy;    //128
	int				usb_psy_ma;  //132
	struct power_supply		batt_psy;   //136, размер 148
	struct power_supply		dc_psy;     // 284
	struct power_supply		*bms_psy;   //432
	int				dc_psy_type;  // 436
	int				dc_psy_ma;    // 440
	const char			*bms_psy_name; // 444

	/* status tracking */
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
  
//**************************************
//*  Конструктор модуля
//**************************************
int smb135x_charger_probe(i2c_client *client, const i2c_device_id *id) {
  
  
struct power_supply* usb_psy;
struct smb135x_chg *chip;

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

chip->work.work.data.counter=-32;
chip->work.work.entry.next=&chip->work.work.entry;
chip->work.work.entry.prev=&chip->work.work.entry;



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
