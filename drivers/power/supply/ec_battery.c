#ifdef CONFIG_ARCH_ADVANTECH
/*
 * Gas Gauge driver for TI's BQ20Z75
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <linux/power/ec_battery.h>

enum {
	REG_MANUFACTURER_DATA,
	REG_TEMPERATURE,
	REG_VOLTAGE,
	REG_CURRENT,
	REG_CAPACITY,
	REG_TIME_TO_EMPTY,
	REG_TIME_TO_FULL,
	REG_STATUS,
	REG_CYCLE_COUNT,
	REG_SERIAL_NUMBER,
	REG_REMAINING_CAPACITY,
	REG_REMAINING_CAPACITY_CHARGE,
	REG_FULL_CHARGE_CAPACITY,
	REG_FULL_CHARGE_CAPACITY_CHARGE,
	REG_DESIGN_CAPACITY,
	REG_DESIGN_CAPACITY_CHARGE,
	REG_DESIGN_VOLTAGE,
};

#define BATTERY_UPDATE_INTERVAL 5*HZ /*seconds*/

/* Battery Mode defines */
#define BATTERY_MODE_OFFSET		0x03
#define BATTERY_MODE_MASK		0x8000
enum bq20z75_battery_mode {
	BATTERY_MODE_AMPS,
	BATTERY_MODE_WATTS
};

/* manufacturer access defines */
#define MANUFACTURER_ACCESS_STATUS	0x0006
#define MANUFACTURER_ACCESS_SLEEP	0x0011

/* battery status value bits */
#define BATTERY_DISCHARGING		0x40
#define BATTERY_FULL_CHARGED		0x20
#define BATTERY_FULL_DISCHARGED		0x10

#define BQ20Z75_DATA(_psp, _addr, _min_value, _max_value) { \
	.psp = _psp, \
	.addr = _addr, \
	.min_value = _min_value, \
	.max_value = _max_value, \
}

static const struct bq20z75_device_data {
	enum power_supply_property psp;
	u8 addr;
	int min_value;
	int max_value;
} bq20z75_data[] = {
	[REG_MANUFACTURER_DATA] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_PRESENT, 0x00, 0, 65535),
	[REG_TEMPERATURE] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_TEMP, 0x08, 0, 65535),
	[REG_VOLTAGE] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_VOLTAGE_NOW, 0x09, 0, 20000),
	[REG_CURRENT] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_CURRENT_NOW, 0x0A, -32768,
			32767),
	[REG_CAPACITY] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_CAPACITY, 0x0D, 0, 100),
	[REG_REMAINING_CAPACITY] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_ENERGY_NOW, 0x0F, 0, 65535),
	[REG_REMAINING_CAPACITY_CHARGE] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_CHARGE_NOW, 0x0F, 0, 65535),
	[REG_FULL_CHARGE_CAPACITY] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_ENERGY_FULL, 0x10, 0, 65535),
	[REG_FULL_CHARGE_CAPACITY_CHARGE] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_CHARGE_FULL, 0x10, 0, 65535),
	[REG_TIME_TO_EMPTY] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG, 0x12, 0,
			65535),
	[REG_TIME_TO_FULL] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_TIME_TO_FULL_AVG, 0x13, 0,
			65535),
	[REG_STATUS] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_STATUS, 0x16, 0, 65535),
	[REG_CYCLE_COUNT] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_CYCLE_COUNT, 0x17, 0, 65535),
	[REG_DESIGN_CAPACITY] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, 0x18, 0,
			65535),
	[REG_DESIGN_CAPACITY_CHARGE] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, 0x18, 0,
			65535),
	[REG_DESIGN_VOLTAGE] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN, 0x19, 0,
			65535),
	[REG_SERIAL_NUMBER] =
		BQ20Z75_DATA(POWER_SUPPLY_PROP_SERIAL_NUMBER, 0x1C, 0, 65535),
};

static enum power_supply_property bq20z75_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
};

struct bq20z75_info {
	struct i2c_client		*client;
	struct power_supply		*power_supply;
	struct bq20z75_platform_data	*pdata;
	bool				is_present;
	bool				gpio_detect;
	bool				enable_detection;
	int				irq;
        struct delayed_work		work;
};

static int bq20z75_read_byte_data(struct i2c_client *client, u8 address)
{
	s32 ret = 0;

	ret = i2c_smbus_read_byte_data(client, address);
               // printk(KERN_INFO "[Battery]--> i2c_smbus_read_byte_data [0x%x] address[0x%x] \n",ret,address);
	if (ret < 0) {
		dev_dbg(&client->dev,
			"%s: i2c read at address 0x%x failed\n",
			__func__, address);
		return ret;
	}
	return ret;
}
static int bq20z75_read_word_data(struct i2c_client *client, u8 address)
{
	struct bq20z75_info *bq20z75_device = i2c_get_clientdata(client);
	s32 ret = 0;
	int retries = 1;

	if (bq20z75_device->pdata)
		retries = max(bq20z75_device->pdata->i2c_retry_count + 1, 1);

	while (retries > 0) {
		ret = i2c_smbus_read_word_data(client, address);
        //        printk(KERN_INFO "[Battery]--> i2c_smbus_write_word_data ret[0x%x] address[0x%x] \n",ret,address);
		if (ret >= 0)
			break;
		retries--;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"%s: i2c read at address 0x%x failed\n",
			__func__, address);
		return ret;
	}

	return le16_to_cpu(ret);
}

static int bq20z75_write_word_data(struct i2c_client *client, u8 address,
	u16 value)
{
	struct bq20z75_info *bq20z75_device = i2c_get_clientdata(client);
	s32 ret = 0;
	int retries = 1;

	if (bq20z75_device->pdata)
		retries = max(bq20z75_device->pdata->i2c_retry_count + 1, 1);

	while (retries > 0) {
		ret = i2c_smbus_write_word_data(client, address,
			le16_to_cpu(value));
		if (ret >= 0)
			break;
		retries--;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"%s: i2c write to address 0x%x failed\n",
			__func__, address);
		return ret;
	}

	return 0;
}

static int bq20z75_get_battery_presence(
 	struct i2c_client *client, enum power_supply_property psp,
 	union power_supply_propval *val)
{
        s32 ret;
        struct bq20z75_info *bq20z75_device = i2c_get_clientdata(client);
        ret = bq20z75_write_word_data(client, 0x82, 0x90); 
        ret = bq20z75_read_byte_data(client, 0x80);

        if (ret < 0)
                return ret;
            val->intval = ret;

	        if (ret & 0x02)
			val->intval = 1;
		else
			val->intval = 0;
		bq20z75_device->is_present = val->intval;
//		printk(KERN_INFO "[Battery]--> present [0x%x][0x%x] \n",bq20z75_device->is_present,val->intval);
		return ret;
}

static int bq20z75_get_battery_health(
	struct i2c_client *client, enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;
	ret = bq20z75_write_word_data(client, 0x82, 0x90);
	ret = bq20z75_read_byte_data(client, 0x80);

	val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;

	if (ret < 0)
	{
		printk("\n[bq20z75_get_battery_health] EC health ERROR! Use Default val->intval:%d\n", val->intval);
		return ret;
	}

	val->intval = ret;

	if (ret & 0x02)
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
	else
		val->intval = POWER_SUPPLY_HEALTH_DEAD;

	//printk(KERN_INFO "[Battery]--> bq20z75_get_battery_health[0x%x] \n",ret);

	return ret;
}

static int bq20z75_get_battery_voltage_property(struct i2c_client *client,
        int reg_offset, enum power_supply_property psp,
        union power_supply_propval *val)
{
        s32 ret, ret1;
        ret = bq20z75_write_word_data(client, 0x82, 0x03); //by clayder
        ret = bq20z75_read_word_data(client, 0x80);
        ret1 = ((ret & 0xff00) >> 8 | (ret & 0x00ff) << 8);
        if (ret < 0)
                return ret1;

//            printk(KERN_INFO "[Battery]--> bq20z75_get_battery_voltage_property[0x%x] \n",ret);
            val->intval = ret1;

        return 0;
}

static int bq20z75_get_battery_current_property(struct i2c_client *client,
        int reg_offset, enum power_supply_property psp,
        union power_supply_propval *val)
{
        s32 ret, ret1;
        ret = bq20z75_write_word_data(client, 0x82, 0x07); //by clayder
        ret = bq20z75_read_word_data(client, 0x80);
        ret1 = ((ret & 0xff00) >> 8 | (ret & 0x00ff) << 8);
        if (ret < 0)
                return ret1;
            val->intval = ret1;

        return 0;
}

static int bq20z75_get_battery_temp_property(struct i2c_client *client,
        int reg_offset, enum power_supply_property psp,
        union power_supply_propval *val)
{
        s32 ret;
        ret = bq20z75_write_word_data(client, 0x82, 0x02); //by clayder
        ret = bq20z75_read_byte_data(client, 0x80);

        if (ret < 0)
                return ret;
            val->intval = ret;

        return 0;
}

static int bq20z75_get_battery_status_property(struct i2c_client *client,
        int reg_offset, enum power_supply_property psp,
        union power_supply_propval *val)
{
        s32 ret;
        struct bq20z75_info *bq20z75_device = i2c_get_clientdata(client);

        ret = bq20z75_write_word_data(client, 0x82, 0x01); //by clayder
        ret = bq20z75_read_byte_data(client, 0x80);

	val->intval = POWER_SUPPLY_STATUS_UNKNOWN;

	if (ret < 0)
	{
		printk("\n[bq20z75_get_battery_status_property] EC status ERROR! Use Default val->intval:%d\n", val->intval);
		return ret;
	}

        /* returned values are 16 bit */
        if (bq20z75_data[reg_offset].min_value < 0)
                ret = (s16)ret;
        //printk(KERN_INFO "[Battery]--> bq20z75_get_battery_status_property[0x%x] \n",ret);
        if (ret >= bq20z75_data[reg_offset].min_value &&
            ret <= bq20z75_data[reg_offset].max_value) {
                val->intval = ret;
                if (psp == POWER_SUPPLY_PROP_STATUS) {
                        if (ret & BATTERY_FULL_CHARGED)
                                val->intval = POWER_SUPPLY_STATUS_FULL;
                        else if (ret & BATTERY_FULL_DISCHARGED)
                                val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
                        else if (ret & BATTERY_DISCHARGING)
                                val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
                        else
                                val->intval = POWER_SUPPLY_STATUS_CHARGING;
                }
        } else {
                if (psp == POWER_SUPPLY_PROP_STATUS)
                        val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
                else
                        val->intval = 0;
        }

        if(!bq20z75_device->is_present)
            val->intval = POWER_SUPPLY_STATUS_UNKNOWN;

        return 0;
}

static int bq20z75_get_battery_property(struct i2c_client *client,
	int reg_offset, enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;

	ret = bq20z75_read_word_data(client,
		bq20z75_data[reg_offset].addr);
	if (ret < 0)
		return ret;

	/* returned values are 16 bit */
	if (bq20z75_data[reg_offset].min_value < 0)
		ret = (s16)ret;

	if (ret >= bq20z75_data[reg_offset].min_value &&
	    ret <= bq20z75_data[reg_offset].max_value) {
		val->intval = ret;
		if (psp == POWER_SUPPLY_PROP_STATUS) {
			if (ret & BATTERY_FULL_CHARGED)
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else if (ret & BATTERY_FULL_DISCHARGED)
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			else if (ret & BATTERY_DISCHARGING)
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
		}
	} else {
		if (psp == POWER_SUPPLY_PROP_STATUS)
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		else
			val->intval = 0;
	}

	return 0;
}

static void  bq20z75_unit_adjustment(struct i2c_client *client,
	enum power_supply_property psp, union power_supply_propval *val)
{
#define BASE_UNIT_CONVERSION		1000
#define BATTERY_MODE_CAP_MULT_WATT	(10 * BASE_UNIT_CONVERSION)
#define TIME_UNIT_CONVERSION		60
#define TEMP_KELVIN_TO_CELSIUS		2731
	switch (psp) {
	case POWER_SUPPLY_PROP_ENERGY_NOW:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		/* bq20z75 provides energy in units of 10mWh.
		 * Convert to µWh
		 */
		val->intval *= BATTERY_MODE_CAP_MULT_WATT;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
//		val->intval *= BASE_UNIT_CONVERSION;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		/* bq20z75 provides battery temperature in 0.1K
		 * so convert it to 0.1°C
		 */
	//	val->intval -= TEMP_KELVIN_TO_CELSIUS;
		break;

	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		/* bq20z75 provides time to empty and time to full in minutes.
		 * Convert to seconds
		 */
		val->intval *= TIME_UNIT_CONVERSION;
		break;

	default:
		dev_dbg(&client->dev,
			"%s: no need for unit conversion %d\n", __func__, psp);
	}
}
/*
static enum bq20z75_battery_mode
bq20z75_set_battery_mode(struct i2c_client *client,
	enum bq20z75_battery_mode mode)
{
	int ret, original_val;
	original_val = bq20z75_read_word_data(client, BATTERY_MODE_OFFSET);
	if (original_val < 0)
		return original_val;
	if ((original_val & BATTERY_MODE_MASK) == mode)
		return mode;
	if (mode == BATTERY_MODE_AMPS)
		ret = original_val & ~BATTERY_MODE_MASK;
	else
		ret = original_val | BATTERY_MODE_MASK;
	ret = bq20z75_write_word_data(client, BATTERY_MODE_OFFSET, ret);
	if (ret < 0)
		return ret;
	return original_val & BATTERY_MODE_MASK;
}
*/
static int bq20z75_get_battery_capacity(struct i2c_client *client,
	int reg_offset, enum power_supply_property psp,
	union power_supply_propval *val)
{
	s32 ret;
        ret = bq20z75_write_word_data(client, 0X82, 0x0D); //by clayder
        ret = bq20z75_read_byte_data(client, 0X80);
	//ret = bq20z75_read_word_data(client, 0X80);
	if (ret < 0)
		return ret;
       // printk(KERN_INFO "[Battery]--> bq20z75_get_battery_capacity ret[0x%x]  \n",ret);
	if (psp == POWER_SUPPLY_PROP_CAPACITY) {
		/* bq20z75 spec says that this can be >100 %
		* even if max value is 100 % */
		val->intval = min(ret, 100);
	} else
		val->intval = ret;

	if (ret < 0)
		return ret;

	return 0;
}

static char bq20z75_model_name[16];
static int bq20z75_get_battery_model_name(struct i2c_client *client,
	union power_supply_propval *val)
{
	int ret;

	ret = sprintf(bq20z75_model_name, "bq20z75");
	val->strval = bq20z75_model_name;

	return 0;
}

static char bq20z75_manufacturer[16];
static int bq20z75_get_battery_manufacturer(struct i2c_client *client,
	union power_supply_propval *val)
{
	int ret;

	ret = sprintf(bq20z75_manufacturer, "bq20z75");
	val->strval = bq20z75_manufacturer;

	return 0;
}

static char bq20z75_serial[5];
static int bq20z75_get_battery_serial_number(struct i2c_client *client,
	union power_supply_propval *val)
{
	int ret;

	ret = bq20z75_read_word_data(client,
		bq20z75_data[REG_SERIAL_NUMBER].addr);
	if (ret < 0)
		return ret;

	ret = sprintf(bq20z75_serial, "%04x", ret);
	val->strval = bq20z75_serial;

	return 0;
}

static int bq20z75_get_property_index(struct i2c_client *client,
	enum power_supply_property psp)
{
	int count;
	for (count = 0; count < ARRAY_SIZE(bq20z75_data); count++)
		if (psp == bq20z75_data[count].psp)
			return count;

	dev_warn(&client->dev,
		"%s: Invalid Property - %d\n", __func__, psp);

	return -EINVAL;
}

static int bq20z75_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = 0;
	struct bq20z75_info *bq20z75_device = power_supply_get_drvdata(psy);
	struct i2c_client *client = bq20z75_device->client;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		ret = bq20z75_get_battery_presence(client, psp, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq20z75_get_battery_health(client, psp, val);
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_ENERGY_NOW:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
                break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = bq20z75_get_battery_capacity(client, ret, psp, val);
		break;

	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		ret = bq20z75_get_battery_serial_number(client, val);
		break;

	case POWER_SUPPLY_PROP_STATUS:
                ret = bq20z75_get_battery_status_property(client, ret, psp, val);
                break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
                break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
                ret = bq20z75_get_battery_voltage_property(client, ret, psp, val);
                break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
                ret = bq20z75_get_battery_current_property(client, ret, psp, val);
                break;
	case POWER_SUPPLY_PROP_TEMP:
                ret = bq20z75_get_battery_temp_property(client, ret, psp, val);
                break;

	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		ret = bq20z75_get_property_index(client, psp);
		if (ret < 0)
			break;

		ret = bq20z75_get_battery_property(client, ret, psp, val);
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	break;

	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
	break;

	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_BATTERY;
	break;

	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_UNKNOWN;
	break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		ret = bq20z75_get_battery_model_name(client, val);
	break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		ret = bq20z75_get_battery_manufacturer(client, val);
	break;

	default:
		dev_err(&client->dev,
			"%s: INVALID property\n", __func__);
		return -EINVAL;
	}

	if (!bq20z75_device->enable_detection)
		goto done;

	if (!bq20z75_device->gpio_detect &&
		bq20z75_device->is_present != (ret >= 0)) {
		bq20z75_device->is_present = (ret >= 0);
		power_supply_changed(bq20z75_device->power_supply);
		printk(KERN_INFO "power_supply_changed\n");    
	}

done:
	if (!ret) {
		/* Convert units to match requirements for power supply class */
		bq20z75_unit_adjustment(client, psp, val);
	}

	dev_dbg(&client->dev,
		"%s: property = %d, value = %x\n", __func__, psp, val->intval);

	if (ret && bq20z75_device->is_present)
		return ret;

	/* battery not present, so return NODATA for properties */
	//if (ret)
	//	return -ENODATA;

	return 0;
}

static irqreturn_t bq20z75_irq(int irq, void *devid)
{
	struct power_supply *battery = devid;

	power_supply_changed(battery);

	return IRQ_HANDLED;
}

static void bq20z75_battery_work(struct work_struct *work)
{
	struct bq20z75_info *bq20z75_device;
	bq20z75_device = container_of(work, struct bq20z75_info, work.work);

	power_supply_changed(bq20z75_device->power_supply);

	/* reschedule for the next time */
	schedule_delayed_work(&bq20z75_device->work, BATTERY_UPDATE_INTERVAL);
}

static const struct power_supply_desc ec_default_desc = {
        .type = POWER_SUPPLY_TYPE_BATTERY,
        .properties = bq20z75_properties,
        .num_properties = ARRAY_SIZE(bq20z75_properties),
        .get_property = bq20z75_get_property,
};

static int bq20z75_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	struct bq20z75_info *bq20z75_device;
	struct power_supply_desc *ec_desc;
	struct bq20z75_platform_data *pdata = client->dev.platform_data;
	struct power_supply_config psy_cfg = {};
	int rc;
	int irq;

        printk(KERN_INFO "[EC_Battery]----> EC  bq20z75_probe\n");

        ec_desc = devm_kmemdup(&client->dev, &ec_default_desc,
                        sizeof(*ec_desc), GFP_KERNEL);
        if (!ec_desc)
                return -ENOMEM;

	ec_desc->name = "battery";

	bq20z75_device = kzalloc(sizeof(struct bq20z75_info), GFP_KERNEL);
	if (!bq20z75_device)
		return -ENOMEM;

	bq20z75_device->client = client;
	bq20z75_device->enable_detection = false;
	bq20z75_device->gpio_detect = false;
        psy_cfg.of_node = client->dev.of_node;
        psy_cfg.drv_data = bq20z75_device;

	if (pdata) {
		bq20z75_device->gpio_detect = gpio_is_valid(pdata->battery_detect);
		bq20z75_device->pdata = pdata;
	}

	i2c_set_clientdata(client, bq20z75_device);

	rc = bq20z75_read_byte_data(bq20z75_device->client, 0x80); //Check whether EC Battery Charger exists
	if(rc < 0) {
		printk("%s: No Battery Charger %d\n",__func__,rc);
		goto exit_psupply;
	}

	if (!bq20z75_device->gpio_detect)
		goto skip_gpio;

	rc = gpio_request(pdata->battery_detect, dev_name(&client->dev));
	if (rc) {
		dev_warn(&client->dev, "Failed to request gpio: %d\n", rc);
		bq20z75_device->gpio_detect = false;
		goto skip_gpio;
	}

	rc = gpio_direction_input(pdata->battery_detect);
	if (rc) {
		dev_warn(&client->dev, "Failed to get gpio as input: %d\n", rc);
		gpio_free(pdata->battery_detect);
		bq20z75_device->gpio_detect = false;
		goto skip_gpio;
	}

	irq = gpio_to_irq(pdata->battery_detect);
	if (irq <= 0) {
		dev_warn(&client->dev, "Failed to get gpio as irq: %d\n", irq);
		gpio_free(pdata->battery_detect);
		bq20z75_device->gpio_detect = false;
		goto skip_gpio;
	}

	rc = request_irq(irq, bq20z75_irq,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		dev_name(&client->dev), bq20z75_device->power_supply);
	if (rc) {
		dev_warn(&client->dev, "Failed to request irq: %d\n", rc);
		gpio_free(pdata->battery_detect);
		bq20z75_device->gpio_detect = false;
		goto skip_gpio;
	}

	bq20z75_device->irq = irq;

skip_gpio:
	bq20z75_device->power_supply = power_supply_register(&client->dev, ec_desc,
						   &psy_cfg);
	if (IS_ERR(bq20z75_device->power_supply)) {
		dev_err(&client->dev,
			"%s: Failed to register power supply\n", __func__);
		rc = PTR_ERR(bq20z75_device->power_supply);
		goto exit_psupply;
	}

	dev_info(&client->dev,
		"%s: battery gas gauge device registered\n", client->name);

	//[Advantech] Add delay work to update battery status frequently
	INIT_DEFERRABLE_WORK(&bq20z75_device->work, bq20z75_battery_work);
	schedule_delayed_work(&bq20z75_device->work, BATTERY_UPDATE_INTERVAL);

	return 0;

exit_psupply:
	if (bq20z75_device->irq)
		free_irq(bq20z75_device->irq, bq20z75_device->power_supply);
	if (bq20z75_device->gpio_detect)
		gpio_free(pdata->battery_detect);

	kfree(bq20z75_device);

	return rc;
}

static int bq20z75_remove(struct i2c_client *client)
{
	struct bq20z75_info *bq20z75_device = i2c_get_clientdata(client);

	if (bq20z75_device->irq)
		free_irq(bq20z75_device->irq, bq20z75_device->power_supply);
	if (bq20z75_device->gpio_detect)
		gpio_free(bq20z75_device->pdata->battery_detect);

	cancel_delayed_work(&bq20z75_device->work);
	flush_scheduled_work();

	power_supply_unregister(bq20z75_device->power_supply);

	kfree(bq20z75_device);
	bq20z75_device = NULL;

	return 0;
}

static void bq20z75_shutdown(struct i2c_client *client)
{
	struct bq20z75_info *bq20z75_device = i2c_get_clientdata(client);

	if (bq20z75_device->irq)
		free_irq(bq20z75_device->irq, &bq20z75_device->power_supply);
	if (bq20z75_device->gpio_detect)
		gpio_free(bq20z75_device->pdata->battery_detect);

	cancel_delayed_work(&bq20z75_device->work);
	flush_scheduled_work();

	power_supply_unregister(bq20z75_device->power_supply);
	kfree(bq20z75_device);
	bq20z75_device = NULL;
}

#if defined CONFIG_PM
static int bq20z75_suspend(struct device *dev)
{
        struct i2c_client *client = to_i2c_client(dev);
	struct bq20z75_info *bq20z75_device = i2c_get_clientdata(client);
	s32 ret;

        cancel_delayed_work(&bq20z75_device->work);

	/* write to manufacturer access with sleep command */
	ret = bq20z75_write_word_data(client,
		bq20z75_data[REG_MANUFACTURER_DATA].addr,
		MANUFACTURER_ACCESS_SLEEP);
	if (bq20z75_device->is_present && ret < 0)
		return ret;
	return 0;
}

static int bq20z75_resume(struct device *dev)
{
        struct i2c_client *client = to_i2c_client(dev);
	struct bq20z75_info *bq20z75_device = i2c_get_clientdata(client);

	schedule_delayed_work(&bq20z75_device->work, BATTERY_UPDATE_INTERVAL);
	return 0;
}

static SIMPLE_DEV_PM_OPS(ec_pm_ops, bq20z75_suspend, bq20z75_resume);
#define EC_PM_OPS (&ec_pm_ops)

#else
#define EC_PM_OPS NULL
//#endif
/* any smbus transaction will wake up bq20z75 */
#endif

static const struct i2c_device_id bq20z75_id[] = {
	{ "ec", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, bq20z75_id);

static struct i2c_driver bq20z75_battery_driver = {
	.probe		= bq20z75_probe,
	.remove		= bq20z75_remove,
	.shutdown	= bq20z75_shutdown,
	.id_table	= bq20z75_id,
	.driver = {
		.name	= "ec-battery",
		.pm	= EC_PM_OPS,
	},
};

static int __init bq20z75_battery_init(void)
{
	return i2c_add_driver(&bq20z75_battery_driver);
}
module_init(bq20z75_battery_init);

static void __exit bq20z75_battery_exit(void)
{
	i2c_del_driver(&bq20z75_battery_driver);
}
module_exit(bq20z75_battery_exit);

MODULE_DESCRIPTION("EC battery monitor driver");
MODULE_LICENSE("GPL");

#endif
