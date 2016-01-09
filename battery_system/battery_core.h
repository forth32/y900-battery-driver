int32_t jrd_qpnp_vadc_read(enum qpnp_vadc_channels channel,struct qpnp_vadc_result *result);
//struct battery_core_interface;

//*************************************************
//* Струтура описания рабочих переменных драйвера
//*************************************************
// все смещения вычислены по исходному дизассемблированному тексту

struct battery_interface {
  struct battery_core_interface* bat;  //0    0 battery_core_interface*
  int (*timer_resume_proc)(struct battery_core_interface*);  //4
  int (*timer_suspend_proc)(struct battery_core_interface*);  //8
  int (*x_timer_suspend_proc)(struct battery_core_interface*,int*);  //12
  int (*alarm_wakeup_proc)(void*);  //16
  char* bname;  //20
  struct battery_interface* thisptr;    //24
  int (*get_vbat_proc)(struct battery_interface*, int*);  //28
  int x32;
  int (*get_vntc_proc)(struct battery_interface*, int*);  //36
  int (*x40)(struct battery_interface*, int*);
  int x44;
  int x48;
  int vbat;   // 52
  int tbat;   // 56
  char* name_rtcdev; //60
  int x64;
  int x68;
  struct rtc_timer rtctimer; //72
  struct rtc_device* rtcfd; //120
  struct device* parent; //124
};

int battery_core_register(struct device* dev, struct battery_interface* api);
void battery_core_unregister(struct device *dev, struct battery_interface *api);
