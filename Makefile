ccflags-$(CONFIG_POWER_SUPPLY_DEBUG) := -DDEBUG

power_supply-y				:= power_supply_core.o
power_supply-$(CONFIG_SYSFS)		+= power_supply_sysfs.o
power_supply-$(CONFIG_LEDS_TRIGGERS)	+= power_supply_leds.o

obj-$(CONFIG_POWER_SUPPLY)	+= power_supply.o
obj-$(CONFIG_GENERIC_ADC_BATTERY)	+= generic-adc-battery.o

obj-$(CONFIG_PDA_POWER)		+= pda_power.o
obj-$(CONFIG_APM_POWER)		+= apm_power.o
obj-$(CONFIG_MAX8925_POWER)	+= max8925_power.o
obj-$(CONFIG_WM831X_BACKUP)	+= wm831x_backup.o
obj-$(CONFIG_WM831X_POWER)	+= wm831x_power.o
obj-$(CONFIG_WM8350_POWER)	+= wm8350_power.o
obj-$(CONFIG_TEST_POWER)	+= test_power.o

obj-$(CONFIG_BATTERY_88PM860X)	+= 88pm860x_battery.o
obj-$(CONFIG_BATTERY_ANDROID)	+= android_battery.o
obj-$(CONFIG_BATTERY_DS2760)	+= ds2760_battery.o
obj-$(CONFIG_BATTERY_DS2780)	+= ds2780_battery.o
obj-$(CONFIG_BATTERY_DS2781)	+= ds2781_battery.o
obj-$(CONFIG_BATTERY_DS2782)	+= ds2782_battery.o
obj-$(CONFIG_BATTERY_GOLDFISH)	+= goldfish_battery.o
obj-$(CONFIG_BATTERY_PMU)	+= pmu_battery.o
obj-$(CONFIG_BATTERY_OLPC)	+= olpc_battery.o
obj-$(CONFIG_BATTERY_TOSA)	+= tosa_battery.o
obj-$(CONFIG_BATTERY_COLLIE)	+= collie_battery.o
obj-$(CONFIG_BATTERY_WM97XX)	+= wm97xx_battery.o
obj-$(CONFIG_BATTERY_SBS)	+= sbs-battery.o
obj-$(CONFIG_BATTERY_BQ27x00)	+= bq27x00_battery.o
obj-$(CONFIG_BATTERY_DA9030)	+= da9030_battery.o
obj-$(CONFIG_BATTERY_DA9052)	+= da9052-battery.o
obj-$(CONFIG_BATTERY_MAX17040)	+= max17040_battery.o
obj-$(CONFIG_BATTERY_MAX17042)	+= max17042_battery.o
obj-$(CONFIG_BATTERY_Z2)	+= z2_battery.o
obj-$(CONFIG_BATTERY_S3C_ADC)	+= s3c_adc_battery.o
obj-$(CONFIG_CHARGER_88PM860X)	+= 88pm860x_charger.o
obj-$(CONFIG_CHARGER_PCF50633)	+= pcf50633-charger.o
obj-$(CONFIG_BATTERY_JZ4740)	+= jz4740-battery.o
obj-$(CONFIG_BATTERY_INTEL_MID)	+= intel_mid_battery.o
obj-$(CONFIG_BATTERY_RX51)	+= rx51_battery.o
obj-$(CONFIG_AB8500_BM)		+= ab8500_bmdata.o ab8500_charger.o ab8500_fg.o ab8500_btemp.o abx500_chargalg.o pm2301_charger.o
obj-$(CONFIG_CHARGER_ISP1704)	+= isp1704_charger.o
obj-$(CONFIG_CHARGER_MAX8903)	+= max8903_charger.o
obj-$(CONFIG_CHARGER_TWL4030)	+= twl4030_charger.o
obj-$(CONFIG_CHARGER_LP8727)	+= lp8727_charger.o
obj-$(CONFIG_CHARGER_LP8788)	+= lp8788-charger.o
obj-$(CONFIG_CHARGER_GPIO)	+= gpio-charger.o
obj-$(CONFIG_CHARGER_MANAGER)	+= charger-manager.o
obj-$(CONFIG_CHARGER_MAX8997)	+= max8997_charger.o
obj-$(CONFIG_CHARGER_MAX8998)	+= max8998_charger.o
obj-$(CONFIG_CHARGER_BQ2415X)	+= bq2415x_charger.o
obj-$(CONFIG_POWER_AVS)		+= avs/
obj-$(CONFIG_SMB349_USB_CHARGER)   += smb349-charger.o
obj-$(CONFIG_SMB350_CHARGER)   += smb350_charger.o
obj-$(CONFIG_SMB135X_CHARGER)   += battery_system/smb135x-charger.o battery_system/charger_core.o
obj-$(CONFIG_SMB1360_CHARGER_FG) += smb1360-charger-fg.o
obj-$(CONFIG_BATTERY_BQ28400)	+= bq28400_battery.o
obj-$(CONFIG_SMB137C_CHARGER)	+= smb137c-charger.o
obj-$(CONFIG_QPNP_BMS)		+= qpnp-bms.o batterydata-lib.o
obj-$(CONFIG_QPNP_VM_BMS)	+= qpnp-vm-bms.o batterydata-lib.o batterydata-interface.o
obj-$(CONFIG_QPNP_CHARGER)	+= qpnp-charger.o
obj-$(CONFIG_QPNP_LINEAR_CHARGER)	+= qpnp-linear-charger.o
obj-$(CONFIG_CHARGER_SMB347)	+= smb347-charger.o
obj-$(CONFIG_CHARGER_TPS65090)	+= tps65090-charger.o
obj-$(CONFIG_BATTERY_BCL)	+= battery_current_limit.o
obj-$(CONFIG_BATTERY_PMD9635)   += battery_system/pmd9635_battery.o battery_system/battery_core.o
obj-$(CONFIG_POWER_RESET)	+= reset/
obj-y				+= qcom/
