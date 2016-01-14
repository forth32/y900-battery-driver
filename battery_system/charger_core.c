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
//* хранилище зарегистрированных зарядников  *
//********************************************
static charger core* registered_chip[10]={0,0,0,0,0,0,0,0,0,0}; 
static int registered_count=0;


//********************************************
//*  Приостановка зарядки 
//********************************************
int charger_core_suspend_charging(void *self) {
  
struct charger_core_interface* chip=self;
struct charger_interface* api;=chip->api;
int rc;

if (chip == 0) return -EINVAL;
api=chip->api;
if (api == 0) return 0;
if (api->enable_charge_fn == 0) return 0;
if (chip->charging_state != POWER_SUPPLY_STATUS_CHARGING) return -EINVAL; 
if (chip->charging_suspend != 0) return -EINVAL;

rc=(*api->enable_charge_fn)(api->parent,0);
if (rc == 0) chip->charging_suspend=1;
return rc;
}

//********************************************
//* Регистрация драйвера зарядника
//********************************************
int charger_core_register(struct device* dev, struct charger_interface* api) {

struct charger_core_interface* chip;
int i; 

if ((dev == 0) || (api == 0)) return -EINVAL;
if (api->parent == 0) return -EINVAL;  // нет собственной управляющей структуры

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

api->self=chip;  // обратная связь от интерфейса charger_core_interface к интерфейсу charger_interface
api->suspend_charging=charger_core_suspend_charging;
api->resume_charging = charger_core_resume_charging;
api->set_charging_current = charger_core_set_charging_current;
api->get_charging_current = charger_core_get_charging_current;
api->get_charger_info = charger_core_get_charger_info;
api->suspend_recharging = charger_core_suspend_recharging;
api->resume_recharging = charger_core_resume_recharging;
api->set_recharging_current = 0;
api->notify_event = charger_core_notify_event;

if (registered_count <9) {
  for (i=0;i<10;i++) {
    if (registered_chip[i] != 0) continue;
    registered_chip[i]=chip;
    registered_count++;
    break;
  }  
}
pr_info("Charger Core Version 4.1.5 (Built at %s %s)!",__DATE__,__TIME__);
return 0; 
}

//*************************************************8
//* Поиск зарядника по имени
//*************************************************8
charger_core_interface* charger_core_get_charger_interface_by_name(const unsigned char* name) {

int i;
  
if (name == 0) return 0;
if (name[0] == 0) return 0;

for (i=0;i<10;i++) {
  if (registered_chip[i] == 0) continue;
  if (strcmp(registered_chip[i]->api.ext_name_battery,name) == 0) return registered_chip[i]->api);
}
return 0;
}

