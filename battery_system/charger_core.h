struct charger_info {
 int charger_status;
 int ichg_now;
 int ada_connected;
 int charging_done;
};

//**************************************
//*  Описатель регулятора напряжения
//**************************************
struct smb135x_regulator {
	struct regulator_desc	rdesc;
	struct regulator_dev	*rdev;
};

//******************************************************************************
//*  Главная интерфейсная структура между драйвером зарядника и charger core
//******************************************************************************
// +48 от начала smb135x_chg - отсюда начинается структура API, передаваемая в charger_core 

struct charger_interface  {
  
  struct charger_interface* self;  // +48 - API charger_interface  

  struct smb135x_chg* parent;        // 92
  int (*set_current_limit_fn)(void *self, int mA);  //96
  int (*enable_charge_fn)(void *self, int enable); // 100
  int (*set_otg_mode_fn)(void *self, int enable); // 104
  char* ext_name_battery; // 108
  char* ext_name_usb;     // 112

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

