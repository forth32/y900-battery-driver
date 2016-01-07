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
#include <linux/workqueue.h>
#include <arch/arm/include/asm/delay.h>
#include "battery_core.h"

#define pr_fmt(fmt) "%s: " fmt, __func__

//*****************************************************
//*  Таблица соответствия напряжения и уровня заряда
//*****************************************************

struct capacity {
  int percent;
  int vmin;
  int vmax;
  int offset;
  int hysteresis;
};
  
// 
struct capacity battery_capacity_table[12]= {
//  %       vmin     vmax   offset hysteresis
   {0,      3100,    3597,    0,    10},
   {1,      3598,    3672,    0,    10},
   {10,     3673,    3735,    0,    10},
   {20,     3736,    3757,    0,    10},
   {30,     3758,    3788,    0,    10},
   {40,     3789,    3832,    0,    10},
   {50,     3833,    3909,    0,    10},
   {60,     3910,    3988,    0,    10},
   {70,     3989,    4072,    0,    10},
   {80,     4073,    4156,    0,    10},
   {90,     4157,    4200,    0,    10},
   {100,    4201,    4500,    0,    10}
};   
#define battery_capacity_table_size 12




//*****************************************************
//*  Таблица перевода напряжения в температуру
//*****************************************************
struct ntc_tvm {
   int tntc;
   int tnvc;
}

struct ntc_tvm ntc_tvm_tables[] = {
// температура  напряжение
     {-45,      1800000},         
     {-40,      1693138},
     {-35,      1665952},
     {-30,      1633668},
     {-25,      1595877},
     {-20,      1551873},
     {-15,      1501569},
     {-10,      1444173},
     {-5,       1379919},
     {0,        1308687},
     {5,        1231323},
     {10,       1149570},
     {15,       1065303},
     {20,       979678},
     {25,       895000},
     {30,       812338},
     {35,       733267},
     {40,       658735},
     {45,       589543},
     {50,       525875},
     {55,       467795},
     {60,       415297},
     {65,       368123},
     {70,       326146},
     {75,       288699},
     {80,       255758},
     {85,       226817},
     {90,       201430},
     {95,       179128},
     {100,      159466},
     {105,      142204},
     {110,      127049},
     {115,      113656},
     {120,      101957},
     {125,      91546}
};    

#define ntc_table_size 35

//*****************************************************
//*   Таблица sysfs-атрибутов
//*****************************************************

struct attribute* _battery_attrs[22]		      
struct device_attribute battery_attrs[21]={
 {{"capacity", 0},                   battery_show_property, battery_store_property},
 {{"ntc", 0},                        battery_show_property, battery_store_property},
 {{"precharge_voltage", 0},          battery_show_property, battery_store_property},
 {{"poweroff_voltage", 0},           battery_show_property, battery_store_property},
 {{"low_voltage", 0},                battery_show_property, battery_store_property},
 {{"recharge_voltage", 0},           battery_show_property, battery_store_property},
 {{"charge_done_votage", 0},         battery_show_property, battery_store_property},
 {{"temp_low_poweroff", 0},          battery_show_property, battery_store_property},
 {{"temp_low_disable_charge", 0},    battery_show_property, battery_store_property},
 {{"temp_high_disable_charge", 0},   battery_show_property, battery_store_property},
 {{"temp_high_poweroff", 0},         battery_show_property, battery_store_property},
 {{"temp_error_margin", 0},          battery_show_property, battery_store_property},
 {{"charging_monitor_period", 0},    battery_show_property, battery_store_property},
 {{"discharging_monitor_period", 0}, battery_show_property, battery_store_property},
 {{"vbat_max", 0},                   battery_show_property, battery_store_property},
 {{"ibat_max", 0},                   battery_show_property, battery_store_property},
 {{"test_mode", 0},                  battery_show_property, battery_store_property},
 {{"disable_charging", 0},           battery_show_property, battery_store_property},
 {{"high_voltage", 0},               battery_show_property, battery_store_property},
 {{"capacity_changed_margin", 0},    battery_show_property, battery_store_property},
 {{"debug_mode", 0},                 battery_show_property, battery_store_property}
}; 

struct attribute_group battery_attr_group={
  "parameters", 
  battery_attr_is_visible, 
  __battery_attrs
}; 

//*****************************************************
//*  Главная интерфейсная структура
//*****************************************************

// 592 байта
struct battery_core {
   struct battery_interface* api;   // 0
   charger_core_interface * charger; // 4
   struct device* dev;  // 8
   struct mutex lock; //12, 40 байт
   int x52;
   struct wakeup_source ws;  // 56, размер 152   
   struct power_supply psy;  // 208, размер 148

   workqueue_struct * mon_queue; // 316

 //---------------------------------  
   struct delayed_work work;     // 320, размер  76:  320-386
   // work_strict work
   //             atomic_long_t data;      320
   //             struct list_head entry;  324-328

   //void (*battery_core_monitor_work)(work_struct *); //332
   struct timer_list timer; //336, размер 52: 336-392

   // void (*function)(unsigned int); //352
//-----------------------------------
//   struct delayed_work* pwork; // 356
   
   int x392;
   int mon_period; // 396
   int x400;
   int new_status;
   int x408;
   char* bname;  //412
   int status;        //416
   int current_now;   //420
   int current_max;   //424
   int volt_now;      //428
   int volt_avg;      //432
   int volt_max;      //436
   int capacity;      //440
   int x444;        
   int x448;
   int present;       //452
   int temp;          //456
   int health;        //460
   int x464;
   int x468;
   int x472;
   int x476;
   int x480;
   int x484;
   int x488;
   int x492;
   int x496;
   int x500;
   int x504;
   int mintemp_dead;   // 508
   int mintemp;   // 512
   
   int maxtemp;
   int maxtemp_dead;    //528
   int x532
   
   int x536;
   int x540;
   struct ntc_tvm* ntc;  // 544
   int ntcsize;
   struct capacity* cap; //552
   int capsize;  //556	
   
   
}   

//*****************************************************
//*  Таблица параметров батарейки
//*****************************************************
static enum power_supply_property battery_core_power_props = {
  POWER_SUPPLY_PROP_STATUS, 
  POWER_SUPPLY_PROP_PRESENT, 
  POWER_SUPPLY_PROP_TEMP, 
  POWER_SUPPLY_PROP_HEALTH, 
  POWER_SUPPLY_PROP_VOLTAGE_NOW, 
  POWER_SUPPLY_PROP_VOLTAGE_AVG
  POWER_SUPPLY_PROP_CAPACITY, 
  POWER_SUPPLY_PROP_VOLTAGE_MAX, 
  POWER_SUPPLY_PROP_CURRENT_MAX, 
  POWER_SUPPLY_PROP_CURRENT_NOW
};  

//*****************************************************
//*  Проверка доступности записи уазанного параметра
//*****************************************************
battery_core_property_is_writeable(power_supply *psy, power_supply_property psp) {
  
if ((psp ==  POWER_SUPPLY_PROP_PRESENT) || 
    (psp == POWER_SUPPLY_PROP_VOLTAGE_NOW) || 
    (psp == POWER_SUPPLY_PROP_TEMP))
    return 1;
return 0;
}

//*****************************************************
//* Чтение параметра батареи
//*****************************************************
int battery_core_get_property(power_supply *psy, power_supply_property psp, power_supply_propval *val) {
  
struct battery_core* bat=container_of(psy, struct battery_core, psy);  

switch(psp){
  case POWER_SUPPLY_PROP_STATUS:
    val->intval=bat->status;
    break;
  
  case POWER_SUPPLY_PROP_HEALTH:
    val->intval=bat->health;
    break;
    
  case POWER_SUPPLY_PROP_PRESENT:  
    val->intval=bat->present;
    break;
    
  case POWER_SUPPLY_PROP_VOLTAGE_MAX:  
    val->intval=bat->volt_max;
    break;
    
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:  
    val->intval=bat->volt_now;
    break;
    
  case POWER_SUPPLY_PROP_VOLTAGE_AVG:  
    val->intval=bat->volt_avg;
    break;
    
  case POWER_SUPPLY_PROP_CURRENT_MAX:  
    val->intval=bat->current_max;
    break;
    
  case POWER_SUPPLY_PROP_CURRENT_NOW:  
    val->intval=bat->current_now;
    break;
    
  case POWER_SUPPLY_PROP_CAPACITY:  
    val->intval=bat->capacity;
    break;
    
  case POWER_SUPPLY_PROP_TEMP:  
    val->intval=bat->temp;
    break;
    
  default:
    pr_err("No such property(%d)\n",psp);
    return -EINVAL;
}
return 0;
}
    
//*****************************************************
//*  Выход из спячки
//*****************************************************
int battery_core_wakeup(void *self) {
  
struct battery_core* bat=self;

if (!bat->ws.active) __pm_stay_awake(&bat->ws);
return 0;
}

//*****************************************************
//* Запись параметра батареи
//*****************************************************
int battery_core_set_property(power_supply *psy, power_supply_property psp, const power_supply_propval *val) {
  
struct battery_core* bat=container_of(psy, struct battery_core, psy);  
int rc=0;

mutex_lock(&bat->lock);

switch (psp) {
  case POWER_SUPPLY_PROP_STATUS:
    bat->status=val->intval;
    break;
  
  case POWER_SUPPLY_PROP_PRESENT:
    if (bat->x468 != 0) bat->present=val->intval;
    else ret=-EPERM;
    break;
  
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:
    if (bat->x468 != 0) bat->volt_now=val->intval;
    else ret=-EPERM;
    break;
    
  case POWER_SUPPLY_PROP_TEMP:
    if (bat->x468 != 0) bat->temp=val->intval;
    else ret=-EPERM;
    break;
    
  default:
    ret=-EINVAL
}    
return ret;
}

//*****************************************************
//*   Установка зарядного тока батареи
//*****************************************************
int battery_core_set_ibat(battery_core *bat, int mA) {

struct charger_core_interface* chg;
struct charger_info chg_info;

if (bat->charger == 0) bat->charger=charger_core_get_charger_interface_by_name(bat->bname);
chg=bat->charger
if (chg != 0) {
  if (chg->x012 != 0) *(chg->x012)(chg->x000,mA);
}

chg_info.ichg_now=0;
chg_info.ada_connected=0;
if (chg != 0) {
  if (chg->x004 != 0) *(chg->x004)(chg->x000,&chg_info);
}

bat->current_now = chg_info.ichg_now*1000;

if (bat->current_now > 0) bat->status=POWER_SUPPLY_STATUS_CHARGING;
else {
  if (chg_info.ada_connected == 0) bat->status=POWER_SUPPLY_STATUS_DISCHARGING;
  else bat->status=POWER_SUPPLY_STATUS_NOT_CHARGING;
}

power_supply_changed(&bat->psy);
return 0;
}

//*****************************************************
//*  Смена состояния источника питания
//*****************************************************
void battery_core_external_power_changed(power_supply *psy) {
  
struct battery_core* bat=container_of(psy, struct battery_core, psy);  
int mA;

if (bat->x472 != 0) mA=0;
else mA=bat->current_max/1000;
battery_core_set_ibat(bat,mA);
}

//*****************************************************
//*  Вычисление среднего
//*****************************************************
int battery_core_calculate_average(int *data) {
  
int i;
int max,min,v;
int sum=0;
max=min=data[0];

for(i=0;i<8;i++) {
  v=data[i];
  if (v<=max) {
    if (v<min) min=v;
  }
  else max=v;
  sum+=v;
}
retrun(sum-max-min)/6;
}

//*****************************************************
//*  Монитор состояния батареи
//*****************************************************
void battery_core_monitor_work(work_struct *work) {
  

int data[8];  
struct workqueue_struct* wq;
struct battery_interface* api;
struct battery_core* bat=container_of(work, struct battery_core, work);  
int i;
int rc;
int ntcsize;
int temp;
int health;
int capacity,volt;
int new_status;  // R6

if (!bat->ws.active) __pm_stay_awake(&bat->ws);
api=bat->api;
memset(data,0,32);
if (api-> get_vntc_proc != 0) {
  r7=-22;
}
else {
  if (api->x40 == 0) goto donetemp;
  else r7=-22;  
}

for (i=0;i<8;i++) {
  if (api-> get_vntc_proc != 0) rc= *(api-> get_vntc_proc)(api->thisptr,&data[i]);
  else if (api-> x40 != 0) rc= *(api-> x40)(api->thisptr,&data[i]);
  if (rc != 0) {
    perror("failed to measure battery temperature, rc=%d\n",rc);
    goto donetemp;
  }
  arm_delay_ops->(*const_udelay)(1073740)
}
if (api-> get_vntc_proc != 0) {
   rc=battery_core_calculate_average(data);
   if (bat->ntc == 0) {
     pr_err("NTC convert table is NULL!\n");
     temp=25;
   }  
   else {
     if (bat->x536 != bat-> x540) rc=rc*bat->x540/bat->x536;
     for(i=0;i<bat->ntcsize-1;i++) {
       if ((rc>bat->ntc[i].tnvc) && (rc<bat->ntc[i+1].tnvc)) temp=bat->ntc[i].tntc;
     }
   }  
   health=POWER_SUPPLY_HEALTH_GOOD;
   if ((temp>maxtemp_dead) || (temp<mintemp_dead)) health=POWER_SUPPLY_HEALTH_DEAD;
   else {
     if (temp>maxtemp) health=POWER_SUPPLY_HEALTH_OVERHEAT;
     if (temp<mintemp) health=POWER_SUPPLY_HEALTH_COLD;
   }
   
   mutex_lock(&bat->lock);
   bat->temp=temp;
   bat->healt=health;
   mutex_unlock(&bat->lock)
}
donetemp:

// Температуру измерили, теперь измеряем напряжение

cap=99;

memset(data,0,32);
if (bat->charger == 0) bat->charger=charger_core_get_charger_interface_by_name(bat->bname);

// Далее следует кучка вызовов charger_core_interface+16
// .text:C03977DC - пока разбирать не будем


// 8 выборок напряжения
if (api-> get_vntc_proc == 0) goto no_vbat_proc;
for (i=0;i<8;i++) {
  rc= *(api-> get_vntc_proc)(api->thisptr,&data[i]);
  if (rc != 0) {
    pr_err("failed to measure battery voltage, rc=%d\n",rc);
    goto no_vbat_proc;
  }  
  arm_delay_ops->(*const_udelay)(1073740)
}
// далее вызывается chg+20, .text:C0397850

volt=battery_core_calculate_average(data);

for (i=0;i<bat->capsize;i++) {
  if ((volt>bat->cap[i].vmin) && (volt<bat->cap[i].vmax)) cap=bat->cap[i].percent;
}

mutex_lock(&bat->lock);
bat->volt_now=volt;
bat->volt_avg=volt;
bat->capacity=cap;
mutex_unlock(&bat->lock);


no_vbat_proc:


volt/=1000;
new_status=bat->status;

switch (bat->status) {
  case POWER_SUPPLY_STATUS_DISCHARGING:
    if (volt<bat->x488) {
      bat->new_status=POWER_SUPPLY_STATUS_CHARGING;
      new_status=POWER_SUPPLY_STATUS_CHARGING;
      break;
    }
    if (volt>=bat->x492) {
      new_status=POWER_SUPPLY_STATUS_UNKNOWN;
      break;
    }
    if (bat->new_status == 2) new_status=POWER_SUPPLY_STATUS_UNKNOWN;
    bat->new_status=new_status;
    break;
   
   case POWER_SUPPLY_STATUS_NOT_CHARGING:
    if (bat-> present == 0) {
      new_status=POWER_SUPPLY_STATUS_UNKNOWN;
      break;
    }
    if (bat-> health != POWER_SUPPLY_HEALTH_GOOD) {
      new_status=POWER_SUPPLY_STATUS_UNKNOWN;
      break;
    }
    if (volt<bat->x500) {
      bat->new_status=new_status;
      break;
    }
    new_status=POWER_SUPPLY_STATUS_UNKNOWN;
    break;

   case POWER_SUPPLY_STATUS_CHARGING:
    new_status=POWER_SUPPLY_STATUS_UNKNOWN;
    bat->new_status=new_status;
    break;
    
   default:
    new_status=POWER_SUPPLY_STATUS_UNKNOWN;
}

// далее следует вызов charger+40 - .text:C0397E8C

if (new_status>3) battery_core_external_power_changed(bat->psy);
queue_delayed_work_on(1,swq,&bat->mon_queue ,msecs_to_jiffies(bat->mon_period);
if (bat->ws.active != 0) __pm_relax(&bat->ws);
}


//*****************************************************
//*  Регистрация ветки параметров в sysfs
//*****************************************************
int battery_core_add_sysfs_interface(device *dev) {
  
int i,rc;
  
if (dev == 0) return -EINVAL;

// формируем массив указателей
for (i=0;i<22;i++) {
  __battery_attrs[i]=&battery_attrs[i];
}

rc=sysfs_create_group(dev->kobj,battery_attr_group);
if (rc != 0) pr_err("failed to add battery attrs!");
}
return rc;
}

//*****************************************************
//*  Регистрация в системе батарейного дарйвера
//*****************************************************
int battery_core_register(struct device* dev, struct battery_interface* api) {
  
struct battery_core* bat;  
struct workqueue_struct* swq;
int rc;

if ((dev==0) || (api==0)) return -EINVAL;
if (api->bname == 0) return -EINVAL;
if (api->bname[0] == 0) return -EINVAL;


bat=kmalloc(sizeof(battery_core),__GFP_ZERO|GFP_KERNEL);
if (bat == 0) {
  pr_err("cannot get enough memory!\n");
  return -ENOMEM;
}

bat->dev=dev;
mutex_init(&bat->lock);
wakeup_source_prepare(&bat->ws, api->bname);
wakeup_source_add(&bat->ws);

bat->status=POWER_SUPPLY_STATUS_DISCHARGING;
bat->volt_max=4350000;
bat->bname=api->bname;
bat->current_max=1000000;
bat->current_now=0;
bat->x444=0;
bat->volt_now=0;
bat->volt_avg=0;
bat->capacity=80;
bat->x448=3;
bat->temp=25;
bat->x476=10;
bat->present=0;
bat->health=POWER_SUPPLY_HEALTH_UNKNOWN;
bat->x536=1800000;
bat->x540=1800000;
bat->x464=0;
bat->x468=0;

bat->ntc=ntc_tvm_tables;
bat->ntcsize=35;
bat->cap=battery_capacity_table;
bat->x556=battery_capacity_table_size;

bat->x480=3000;
bat->x472=0;
bat->x488=3064;
bat->x568=0;
bat->x484=3264;
bat->x572=0;
bat->x576=0;
bat->x580=0;
bat->x584=0;
bat->x588=0;
bat->x492=3600;
bat->x532=2;
bat->x496=4450;
bat->x500=4200;
bat->x504=4350;
bat->mintemp_dead=-20;
bat->mintemp=-5;
bat->x516=-3;
bat->x520=53;
bat->maxtemp=55;
bat->maxtemp_dead=65;

// кросс-ссылки структур друг на друга
bat->api=api;
api->bat=bat;

bat->charger=charger_core_get_charger_interface_by_name(api->bname);
api->x_timer_suspend_proc=0;
api->alarm_wakeup_proc=battery_core_wakeup;
api->timer_resume_proc=0;
api->timer_suspend_proc=0;

bat->mon_period=20000;
bat->x400=25000;
bat->x320=-32;

bat->work.work.next=&bat->work.work.next;
bat->work.work.prev=&bat->work.work.next;

bat->new_status=0;
bat->x408=0;
bat->work.func=battery_core_monitor_work;

init_timer_key(&bat->timer,2,0,0);

bat->timer.data=(unsigned int)(&bat->work);


bat->timer.function=delayed_work_timer_fn;
bat->mon_queue = alloc_workqueue("batt_monitor_wq", WQ_MEM_RECLAIM, 1);
swq=bat->mon_queue;
if (bat->mon_queue == 0) {
  pr_err("failed to create_workqueue batt_monitor_wq!");
  swq=*system_wq;
}
queue_delayed_work_on(1,swq,&bat->work ,msecs_to_jiffies(250);

bat->psy.name=bat->bname;
bat->psy.type=1;
bat->psy.properties=battery_core_power_props;
bat->psy.num_properties=10;
bat->psy.get_property=battery_core_get_property;
bat->psy.set_property=battery_core_set_property;
bat->psy.property_is_writeable=battery_core_property_is_writeable;
bat->psy.external_power_changed=battery_core_external_power_changed;

rc=power_supply_register(bat->dev,&bat->psy);
if (rc<0) {
  pr_err("failed to register psy_%s\n",bat->psy.name);
  goto err_power_supply_register_bat;
}

rc=battery_core_add_sysfs_interface(bat->psy.dev)
if (rc < 0) {
  pr_err("failed to add sysfs interface!\n");
  power_supply_unregister(&bat->psy);
  goto err_power_supply_register_bat;
}

battery_core_external_power_changed(&bat->psy);
pr_err("Battery Core Version %s(Built at %s %s)!","4.1.5f",__DATE__,__TIME__);
return 0;

// Обработка ошибок
err_power_supply_register_bat:

cancel_delayed_work_sync(&bat->work);
if (bat->mon_queue != 0) destroy_workqueue(bat->mon_queue);
wakeup_source_remove(&bat->ws);
wakeup_source_drop(&bat->ws);
mutex_destroy(&bat->lock);
kfree(bat);
return rc;
}

//*****************************************************
//*  Отключение батарейного дарйвера
//*****************************************************
void battery_core_unregister(device *dev, battery_interface *api) {
  
if ((api== 0) || (api->bat == 0)) return;
battery_core_remove_sysfs_interface(dev);
power_supply_unregister(&bat->psy);
if (bat->mon_queue != 0) destroy_workqueue(bat->mon_queue);
wakeup_source_remove(&bat->ws);
wakeup_source_drop(&bat->ws);
mutex_destroy(&bat->mutex);
kfree(bat);
}



		      
		      