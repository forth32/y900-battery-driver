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

struct charger_core_interface {

 struct charger_interface* api; // 0
 struct device* dev; // 4
 struct mutex	mutx;   // 8, 40 байт
 int ci48;
 int ci52;
 
 int ci56;
 int ci60;
 int ci64;
 int ci68;
 int ci72;
 int ci76;
 int ci80;
 
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
chip->ci64=0;
chip->ci68=0;
chip->ci52=2000;
chip->ci60=3;
chip->ci48=2000;
chip->ci72=2000;
chip->ci76=3;
chip->ci56=0;
chip->ci80=0;

*api=chip;

