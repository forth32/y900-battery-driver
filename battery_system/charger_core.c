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

//*************************************************************
//* Структура интерфейса между charger_core и battery_core
//*************************************************************
// оригинал - 84 байта
struct charger_core_interface {

 struct charger_interface* api; // 0
 struct device* dev; // 4
 struct mutex	mutx;   // 8, 40 байт
 int ibat_max;    // 48 
 int ichg_max;    // 52
 
 int ichg_now;    // 56
 int charging_state;    // 60
 int charging_suspend;  // 64
 int charging_done;  // 68 
 int irechg_max;  // 72
 int recharging_state;  // 76
 int recharging_suspend;  // 80
 
} 
 
//********************************************
//* Регистрация драфвра зарядника
//********************************************
int __fastcall charger_core_register(struct device* dev, struct charger_interface* api) {

struct charger_core_interface* chip;
  
if ((dev == 0) || (api == 0)) return -EINVAL;
if (api->self == 0) return -EINVAL;

chip=kzalloc(sizeof(struct charger_core_interface),GFP_KERNEL);
if (chip == 0) {
  pr_err("cannot allocate memory!\n");
  return -ENOMEM;
}
chip->dev=dev;
mitex_init(&chip->mutx);
chip->api=api;
chip->charging_suspend=0;
chip->charging_done=0;
chip->ichg_max=2000;
chip->charging_state=3;
chip->ibat_max=2000;
chip->irechg_max=2000;
chip->recharging_state=3;
chip->ichg_now=0;
chip->recharging_suspend=0;

*api=chip;

