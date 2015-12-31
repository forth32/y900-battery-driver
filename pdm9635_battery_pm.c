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

int32_t jrd_qpnp_vadc_read(enum qpnp_vadc_channels channel,struct qpnp_vadc_result *result);


//*************************************************
//* Струтура описания рабочих переменных драйвера
//*************************************************

struct battery_interface {
  char* bname;  
  int (*get_vbat_proc)(struct battery_interface*, int*);  
  int (*get_vntc_proc)(struct battery_interface*, int*);  
  int vbat;   
  int tbat;
  struct power_supply psy;
  struct device* dev; 
};


//**************************************
//* Возобновление работы модуля
//**************************************
int pmd9635_battery_resume(struct platform_device* pdev) {
return 0;
}

//**************************************
//* Приостановка модуля
//**************************************
int pmd9635_battery_suspend(struct platform_device* pdev, pm_message_t state) {
return 0;
}

//**************************************
//*  Чтение канала АЦП
//**************************************
int pmd9635_get_adc_value(int channel,int* val) {
  
const char* procname="pmd9635_get_adc_value";
int ret;
struct qpnp_vadc_result stor;

if (val == 0) {
  pr_err("%s: Pointer of val is null\n",procname);
  return -EINVAL;
}
ret=jrd_qpnp_vadc_read(channel,&stor);
*val=stor.physical;
if (ret == 0) return 0;
pr_err("%s: can't get adc value from channel %d, rc=%d",procname,channel,ret);
return ret;
}

//**************************************
//*  Чтение напряжения аккумулятора
//**************************************
int pmd9635_battery_get_vbat(struct battery_interface* batdata, int* val) {

int vbat_channel;  
int ret;

if ((batdata == 0) || (val == 0)) return -EINVAL;

vbat_channel=batdata->vbat;
ret=pmd9635_get_adc_value(vbat_channel,val);
if (ret == 0) return 0;
pr_err("pmd9635_battery_get_vbat: can't get battery voltage, rc=%d\n",ret);
return ret;
}

//**************************************
//*  Чтение температуры аккумулятора
//**************************************
int pmd9635_battery_get_vntc(struct battery_interface* batdata, int* val) {

int tbat_channel;  
int ret;

if ((batdata == 0) || (val == 0)) return -EINVAL;

tbat_channel=batdata->tbat;
ret=pmd9635_get_adc_value(tbat_channel,val);
if (ret == 0) return 0;
pr_err("pmd9635_battery_get_vntc: can't get battery temperature, rc=%d\n",ret);
return ret;
}


  
//***********************************************
//*  Конструктор модуля
//***********************************************
static int pmd9635_battery_probe(struct platform_device *pdev) {


const char* procname="pmd9635_battery_probe"; 
static char* bname="battery";
int ret;

struct battery_interface* batdata;
int vbat_channel, tbat_channel;
struct device* dparent;

if ((pdev == 0) || (pdev->dev.of_node == 0)) return -EINVAL;

batdata=kmalloc(sizeof(struct battery_interface),__GFP_ZERO|GFP_KERNEL);
if (batdata == 0) {
  pr_err("%s: Can't allocate memory!",procname);
  return -ENOMEM;
}

dparent=pdev->dev.parent;
batdata->parent=dparent;

batdata->bname=bname;

//		.properties		= s3c_adc_main_bat_props,
//		.num_properties		= ARRAY_SIZE(s3c_adc_main_bat_props),
//		.get_property		= s3c_adc_bat_get_property,
//		.external_power_changed = s3c_adc_bat_ext_power_changed,

strcpy(batdata->psy.name,"pmd9635-battery");
batdata->psy.type=POWER_SUPPLY_TYPE_BATTERY;
batdata->psy.num_properties=0;
batdata->psy.use_for_apm=1;
batdata->psy.status = POWER_SUPPLY_STATUS_DISCHARGING;
dev_set_drvdata(&pdev->dev,batdata);

if (of_property_read_u32_array(pdev->dev.of_node, "pmd9635-battery,vbat-channel", &vbat_channel, 1) != 0) {
  pr_err("%s: failed to get vbat channel!\n",procname);
  return -EPERM;
}
batdata->vbat=vbat_channel;
  
if (of_property_read_u32_array(pdev->dev.of_node, "pmd9635-battery,tbat-channel", &tbat_channel, 1) != 0) {
  pr_err("%s: failed to get tbat channel!\n",procname);
  return -EPERM;
}
batdata->tbat=tbat_channel;

if ((vbat_channel<0) && (tbat_channel<0)) {
  dev_set_drvdata(&pdev->dev,0);
  kfree(batdata);
  return 0;
}
 
if (vbat_channel>=0)  batdata->get_vbat_proc=&pmd9635_battery_get_vbat;
 else batdata->get_vbat_proc=0;

if (tbat_channel>=0)  batdata->get_vntc_proc=&pmd9635_battery_get_vntc;
 else batdata->get_vntc_proc=0;
 
ret = power_supply_register(&pdev->dev, &batdata->psy);
if (ret != 0) {
  pr_err("%s: fail to register battery core, rc=%d!\n",procname,ret);
  dev_set_drvdata(dparent,0);
  kfree(batdata);
  return ret;
}
 
printk(KERN_INFO "%s: vbat_channel=%d, tbat_channel=%d\n",procname,vbat_channel,tbat_channel);
return 0;
}


//**************************************
//* Деструктор модуля
//**************************************
static int pmd9635_battery_remove(struct platform_device *pdev) {
struct device* dparent;
struct battery_interface* batdata;

dparent=&pdev->dev;
batdata=dev_get_drvdata(dparent);

kfree(batdata);
return 0;
}


//**************************************
//*  Структуры данных описания модуля
//**************************************

struct of_device_id pmd9635_battery_match={
  .compatible="qcom,pmd9635-battery"
};  

static struct platform_driver pmd9635_battery_driver = {
	.driver = {
		   .name = "pmd9635_battery",
		   .owner = THIS_MODULE,
		   .of_match_table = &pmd9635_battery_match
	},
	.probe = pmd9635_battery_probe,
	.remove = pmd9635_battery_remove,
	.suspend = pmd9635_battery_suspend,
 	.resume = pmd9635_battery_resume
};

module_platform_driver(pmd9635_battery_driver);

MODULE_DESCRIPTION("pmd9635 Battery driver");
MODULE_LICENSE("GPL");
