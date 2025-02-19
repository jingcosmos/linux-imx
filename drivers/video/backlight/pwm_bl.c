// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple PWM based backlight control, board code has to setup
 * 1) pin configuration so PWM waveforms can output
 * 2) platform_data being correctly configured
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#ifdef CONFIG_ARCH_ADVANTECH
#include <linux/of_gpio.h>
#include <linux/delay.h>
#endif

struct pwm_bl_data {
	struct pwm_device	*pwm;
	struct device		*dev;
	unsigned int		lth_brightness;
	unsigned int		*levels;
	bool			enabled;
	struct regulator	*power_supply;
	struct gpio_desc	*enable_gpio;
	unsigned int		scale;
	bool			legacy;
	unsigned int		post_pwm_on_delay;
	unsigned int		pwm_off_delay;
	int			(*notify)(struct device *,
					  int brightness);
	void			(*notify_after)(struct device *,
					int brightness);
	int			(*check_fb)(struct device *, struct fb_info *);
	void			(*exit)(struct device *);
	char			fb_id[16];
};

static void pwm_backlight_power_on(struct pwm_bl_data *pb)
{
	struct pwm_state state;
	int err;

	pwm_get_state(pb->pwm, &state);
	if (pb->enabled)
		return;

	err = regulator_enable(pb->power_supply);
	if (err < 0)
		dev_err(pb->dev, "failed to enable power supply\n");

	state.enabled = true;
	pwm_apply_state(pb->pwm, &state);

	if (pb->post_pwm_on_delay)
		msleep(pb->post_pwm_on_delay);

	if (pb->enable_gpio)
		gpiod_set_value_cansleep(pb->enable_gpio, 1);

	pb->enabled = true;
}

static void pwm_backlight_power_off(struct pwm_bl_data *pb)
{
	struct pwm_state state;

	pwm_get_state(pb->pwm, &state);
	if (!pb->enabled)
		return;

	if (pb->enable_gpio)
		gpiod_set_value_cansleep(pb->enable_gpio, 0);

	if (pb->pwm_off_delay)
		msleep(pb->pwm_off_delay);

	state.enabled = false;
	state.duty_cycle = 0;
	pwm_apply_state(pb->pwm, &state);

	regulator_disable(pb->power_supply);
	pb->enabled = false;
}

#ifdef CONFIG_ARCH_ADVANTECH
int lvds_vcc_enable;
int lvds_bkl_enable;
int bklt_vcc_enable;
int lvds_stby_enable;
int lvds_reset_enable;

int lvds_vcc_delay_value;
int lvds_bkl_delay_value;
int bklt_pwm_delay_value;
int bklt_en_delay_value;
int lvds_bkl_disable_delay_value;
int bklt_en_disable_delay_value;
enum of_gpio_flags lvds_vcc_flag;
enum of_gpio_flags lvds_bkl_flag;
enum of_gpio_flags bklt_vcc_flag;
enum of_gpio_flags lvds_reset_flag;
enum of_gpio_flags lvds_stby_flag;

void enable_lcd_vdd_en(void)
{
	/* LVDS Panel power enable */
	if (lvds_vcc_enable >= 0)
	{
		printk(KERN_INFO "[LVDS Sequence] 1 Start to enable LVDS VDD. lvds_vcc_flag=%d\n",lvds_vcc_flag);
		gpio_set_value_cansleep(lvds_vcc_enable, lvds_vcc_flag);
	}

	printk(KERN_INFO "[LVDS Sequence] 2 Start to enable LVDS signal.\n");
}

void enable_bridge_stdy_en(void)
{
	/* Bridge IC　standby */

	if (lvds_stby_enable < 0)
		return;
	printk(KERN_INFO "[LVDS Sequence] Bridge IC  standby and reset enable\n");

	gpio_direction_output(lvds_stby_enable, lvds_stby_flag);
	msleep(10);
	gpio_direction_output(lvds_reset_enable, 1);
}

void disable_bridge_stdy_en(void)
{
	/* Bridge IC　standby */
	if (lvds_stby_enable < 0)
		return;
	printk(KERN_INFO "[LVDS Sequence] Bridge IC  standby and reset disable \n");

	gpio_direction_output(lvds_reset_enable, 0);
	msleep(10);
	gpio_direction_output(lvds_stby_enable, 0);

}

void enable_ldb_signal(void)
{		
	mdelay(lvds_vcc_delay_value); // T2 for AUO 7"
}

void enable_ldb_bkl_vcc(void)
{
	mdelay(lvds_bkl_delay_value); // T3 for AUO 7"

	// Backlight On (VCC)
	if (bklt_vcc_enable >= 0)
	{
		printk(KERN_INFO "[LVDS Sequence] 3 Start to enable backlight VCC. bklt_vcc_flag=%d\n",bklt_vcc_flag);
		gpio_set_value_cansleep(bklt_vcc_enable, bklt_vcc_flag);
	}

	mdelay(bklt_pwm_delay_value); // T8 for AUO 7"
	printk(KERN_INFO "[LVDS Sequence] 4 Start to enable backlight PWM.\n");
}

void enable_ldb_bkl_pwm(void)
{
	mdelay(bklt_en_delay_value); // T9 for AUO 7"

	// Backlight Enable (Display On/Off)
        if (lvds_bkl_enable >= 0)
	{
		printk(KERN_INFO "[LVDS Sequence] 5 Start to enable LVDS backlight.\n");
		gpio_set_value_cansleep(lvds_bkl_enable, lvds_bkl_flag);
	}
}

void disable_lcd_vdd_en(void)
{
	if (lvds_vcc_enable >= 0)
	{
		printk(KERN_INFO "[LVDS Sequence] 5 Start to disable LVDS VDD.\n");
		gpio_set_value_cansleep(lvds_vcc_enable, (lvds_vcc_flag)?0:1);
	}

}

void disable_ldb_bkl_vcc(void)
{
	mdelay(lvds_bkl_disable_delay_value);

	if (bklt_vcc_enable >= 0)
	{
		printk(KERN_INFO "[LVDS Sequence] 3 Start to disable backlight VCC.\n");
		gpio_set_value_cansleep(bklt_vcc_enable, (bklt_vcc_flag)?0:1);
	}

	printk(KERN_INFO "[LVDS Sequence] 4 Start to disable LVDS signal.\n");
}

void disable_ldb_bkl_pwm(void)
{
	if (lvds_bkl_enable >= 0)
	{
		printk(KERN_INFO "[LVDS Sequence] 1 Start to disable LVDS backlight.\n");
		gpio_set_value_cansleep(lvds_bkl_enable, (lvds_bkl_flag)?0:1);
	}

	mdelay(bklt_en_disable_delay_value);
	printk(KERN_INFO "[LVDS Sequence] 2 Start to disable backlight PWM.\n");
}

#endif
static int compute_duty_cycle(struct pwm_bl_data *pb, int brightness)
{
	unsigned int lth = pb->lth_brightness;
	struct pwm_state state;
	u64 duty_cycle;

	pwm_get_state(pb->pwm, &state);

	if (pb->levels)
		duty_cycle = pb->levels[brightness];
	else
		duty_cycle = brightness;

	duty_cycle *= state.period - lth;
	do_div(duty_cycle, pb->scale);

	return duty_cycle + lth;
}

static int pwm_backlight_update_status(struct backlight_device *bl)
{
	struct pwm_bl_data *pb = bl_get_data(bl);
	int brightness = backlight_get_brightness(bl);
	struct pwm_state state;


#ifndef CONFIG_ARCH_ADVANTECH
	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;
#endif
	if (pb->notify)
		brightness = pb->notify(pb->dev, brightness);

	if (brightness > 0) {
		pwm_get_state(pb->pwm, &state);
		state.duty_cycle = compute_duty_cycle(pb, brightness);
		pwm_apply_state(pb->pwm, &state);
		pwm_backlight_power_on(pb);
	} else {
		pwm_backlight_power_off(pb);
	}

	if (pb->notify_after)
		pb->notify_after(pb->dev, brightness);

	return 0;
}

static int pwm_backlight_check_fb(struct backlight_device *bl,
				  struct fb_info *info)
{
	struct pwm_bl_data *pb = bl_get_data(bl);

	return !pb->check_fb || pb->check_fb(pb->dev, info);
}

static const struct backlight_ops pwm_backlight_ops = {
	.update_status	= pwm_backlight_update_status,
	.check_fb	= pwm_backlight_check_fb,
};

#ifdef CONFIG_OF
#define PWM_LUMINANCE_SHIFT	16
#define PWM_LUMINANCE_SCALE	(1 << PWM_LUMINANCE_SHIFT) /* luminance scale */

/*
 * CIE lightness to PWM conversion.
 *
 * The CIE 1931 lightness formula is what actually describes how we perceive
 * light:
 *          Y = (L* / 903.3)           if L* ≤ 8
 *          Y = ((L* + 16) / 116)^3    if L* > 8
 *
 * Where Y is the luminance, the amount of light coming out of the screen, and
 * is a number between 0.0 and 1.0; and L* is the lightness, how bright a human
 * perceives the screen to be, and is a number between 0 and 100.
 *
 * The following function does the fixed point maths needed to implement the
 * above formula.
 */
static u64 cie1931(unsigned int lightness)
{
	u64 retval;

	/*
	 * @lightness is given as a number between 0 and 1, expressed
	 * as a fixed-point number in scale
	 * PWM_LUMINANCE_SCALE. Convert to a percentage, still
	 * expressed as a fixed-point number, so the above formulas
	 * can be applied.
	 */
	lightness *= 100;
	if (lightness <= (8 * PWM_LUMINANCE_SCALE)) {
		retval = DIV_ROUND_CLOSEST(lightness * 10, 9033);
	} else {
		retval = (lightness + (16 * PWM_LUMINANCE_SCALE)) / 116;
		retval *= retval * retval;
		retval += 1ULL << (2*PWM_LUMINANCE_SHIFT - 1);
		retval >>= 2*PWM_LUMINANCE_SHIFT;
	}

	return retval;
}

/*
 * Create a default correction table for PWM values to create linear brightness
 * for LED based backlights using the CIE1931 algorithm.
 */
static
int pwm_backlight_brightness_default(struct device *dev,
				     struct platform_pwm_backlight_data *data,
				     unsigned int period)
{
	unsigned int i;
	u64 retval;

	/*
	 * Once we have 4096 levels there's little point going much higher...
	 * neither interactive sliders nor animation benefits from having
	 * more values in the table.
	 */
	data->max_brightness =
		min((int)DIV_ROUND_UP(period, fls(period)), 4096);

	data->levels = devm_kcalloc(dev, data->max_brightness,
				    sizeof(*data->levels), GFP_KERNEL);
	if (!data->levels)
		return -ENOMEM;

	/* Fill the table using the cie1931 algorithm */
	for (i = 0; i < data->max_brightness; i++) {
		retval = cie1931((i * PWM_LUMINANCE_SCALE) /
				 data->max_brightness) * period;
		retval = DIV_ROUND_CLOSEST_ULL(retval, PWM_LUMINANCE_SCALE);
		if (retval > UINT_MAX)
			return -EINVAL;
		data->levels[i] = (unsigned int)retval;
	}

	data->dft_brightness = data->max_brightness / 2;
	data->max_brightness--;

	return 0;
}

static int pwm_backlight_check_fb_name(struct device *dev, struct fb_info *info)
{
	struct backlight_device *bl = dev_get_drvdata(dev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	if (strcmp(info->fix.id, pb->fb_id) == 0)
		return true;

	return false;
}

static int pwm_backlight_parse_dt(struct device *dev,
				  struct platform_pwm_backlight_data *data)
{
	struct device_node *node = dev->of_node;
	unsigned int num_levels = 0;
	unsigned int levels_count;
	unsigned int num_steps = 0;
	struct property *prop;
	unsigned int *table;
	int length;
	u32 value;
	int ret;
	const char *names;

	if (!node)
		return -ENODEV;

	memset(data, 0, sizeof(*data));

	if (!of_property_read_string(node, "fb-names", &names)) {
		strcpy(data->fb_id, names);
		data->check_fb = &pwm_backlight_check_fb_name;
	}

	/*
	 * These values are optional and set as 0 by default, the out values
	 * are modified only if a valid u32 value can be decoded.
	 */
	of_property_read_u32(node, "post-pwm-on-delay-ms",
			     &data->post_pwm_on_delay);
	of_property_read_u32(node, "pwm-off-delay-ms", &data->pwm_off_delay);

	/*
	 * Determine the number of brightness levels, if this property is not
	 * set a default table of brightness levels will be used.
	 */
	prop = of_find_property(node, "brightness-levels", &length);
	if (!prop)
		return 0;

	data->max_brightness = length / sizeof(u32);

	/* read brightness levels from DT property */
	if (data->max_brightness > 0) {
		size_t size = sizeof(*data->levels) * data->max_brightness;
		unsigned int i, j, n = 0;

		data->levels = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!data->levels)
			return -ENOMEM;

		ret = of_property_read_u32_array(node, "brightness-levels",
						 data->levels,
						 data->max_brightness);
		if (ret < 0)
			return ret;

		ret = of_property_read_u32(node, "default-brightness-level",
					   &value);
		if (ret < 0)
			return ret;

		data->dft_brightness = value;

		/*
		 * This property is optional, if is set enables linear
		 * interpolation between each of the values of brightness levels
		 * and creates a new pre-computed table.
		 */
		of_property_read_u32(node, "num-interpolated-steps",
				     &num_steps);

		/*
		 * Make sure that there is at least two entries in the
		 * brightness-levels table, otherwise we can't interpolate
		 * between two points.
		 */
		if (num_steps) {
			if (data->max_brightness < 2) {
				dev_err(dev, "can't interpolate\n");
				return -EINVAL;
			}

			/*
			 * Recalculate the number of brightness levels, now
			 * taking in consideration the number of interpolated
			 * steps between two levels.
			 */
			for (i = 0; i < data->max_brightness - 1; i++) {
				if ((data->levels[i + 1] - data->levels[i]) /
				   num_steps)
					num_levels += num_steps;
				else
					num_levels++;
			}
			num_levels++;
			dev_dbg(dev, "new number of brightness levels: %d\n",
				num_levels);

			/*
			 * Create a new table of brightness levels with all the
			 * interpolated steps.
			 */
			size = sizeof(*table) * num_levels;
			table = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!table)
				return -ENOMEM;

			/* Fill the interpolated table. */
			levels_count = 0;
			for (i = 0; i < data->max_brightness - 1; i++) {
				value = data->levels[i];
				n = (data->levels[i + 1] - value) / num_steps;
				if (n > 0) {
					for (j = 0; j < num_steps; j++) {
						table[levels_count] = value;
						value += n;
						levels_count++;
					}
				} else {
					table[levels_count] = data->levels[i];
					levels_count++;
				}
			}
			table[levels_count] = data->levels[i];

			/*
			 * As we use interpolation lets remove current
			 * brightness levels table and replace for the
			 * new interpolated table.
			 */
			devm_kfree(dev, data->levels);
			data->levels = table;

			/*
			 * Reassign max_brightness value to the new total number
			 * of brightness levels.
			 */
			data->max_brightness = num_levels;
		}

		data->max_brightness--;
	}
#ifdef CONFIG_ARCH_ADVANTECH
	lvds_vcc_enable = of_get_named_gpio_flags(node, "lvds-vcc-enable", 0, &lvds_vcc_flag);
	bklt_vcc_enable = of_get_named_gpio_flags(node, "bklt-vcc-enable", 0, &bklt_vcc_flag);
	lvds_bkl_enable = of_get_named_gpio_flags(node, "lvds-bkl-enable", 0, &lvds_bkl_flag);

	lvds_reset_enable = of_get_named_gpio_flags(node, "lvds-reset", 0, &lvds_reset_flag);
	lvds_stby_enable = of_get_named_gpio_flags(node, "lvds-stby", 0, &lvds_stby_flag);


	if (of_find_property(node, "skip-gpios-init", NULL)) {
		printk("[LVDS] Skip setting GPIOs to default states\n");
		goto get_delays;
	}
	/* Set default to output */
	if (lvds_vcc_enable >= 0)
	{
		ret = gpio_request(lvds_vcc_enable,"lvds_vcc_enable");

		if (ret < 0)
			printk("\nRequest lvds_vcc_enable failed!!\n");
		else
			gpio_direction_output(lvds_vcc_enable, (lvds_vcc_flag)?0:1);
	}

	if (bklt_vcc_enable >= 0)
	{
		ret = gpio_request(bklt_vcc_enable,"bklt_vdd_enable");

		if (ret < 0)
			printk("\nRequest bklt_vdd_enable failed!!\n");
		else
			gpio_direction_output(bklt_vcc_enable, (bklt_vcc_flag)?0:1);
	}
	if (lvds_bkl_enable >= 0)
        {
		ret = gpio_request(lvds_bkl_enable,"lvds_bkl_enable");

		if (ret < 0)
			printk("\nRequest lvds_bkl_enable failed!!\n");
		else
			gpio_direction_output(lvds_bkl_enable, (lvds_bkl_flag)?0:1);
	}

	if (lvds_reset_enable >= 0)
	{
		ret = gpio_request(lvds_reset_enable, "lvds_reset_enable");

		if (ret < 0)
			printk("\nRequest lvds_reset_enable failed!!\n");
		else
		{
			gpio_direction_output(lvds_reset_enable, 0);

		}
	}

	if (lvds_stby_enable >= 0)
	{
		ret = gpio_request(lvds_stby_enable, "lvds_stby_enable");

		if (ret < 0)
			printk("\nRequest lvds_stby_enable failed!!\n");
		else
			gpio_direction_output(lvds_stby_enable, lvds_stby_flag);
	}



get_delays:
	ret = of_property_read_u32(node,"lvds-vcc-delay-time",&lvds_vcc_delay_value);
	if (ret < 0)
	{
		lvds_vcc_delay_value = 25;
	}
	ret = of_property_read_u32(node,"lvds-bkl-delay-time",&lvds_bkl_delay_value);
	if (ret < 0)
	{
		lvds_bkl_delay_value = 210;
	}
	ret = of_property_read_u32(node,"bklt-pwm-delay-time",&bklt_pwm_delay_value);
	if (ret < 0)
	{
		bklt_pwm_delay_value = 20;
	}
	ret = of_property_read_u32(node,"bklt-en-delay-time",&bklt_en_delay_value);
	if (ret < 0)
	{
		bklt_en_delay_value = 20;
	}
	ret = of_property_read_u32(node,"bklt-en-disable-delay-time",&lvds_bkl_disable_delay_value);
	if (ret < 0)
	{
		lvds_bkl_disable_delay_value = 50;
	}
	ret = of_property_read_u32(node,"bklt-en-disable-delay-time",&lvds_bkl_disable_delay_value);
	if (ret < 0)
	{
		lvds_bkl_disable_delay_value = 10;
	}
	ret = of_property_read_u32(node,"bklt-en-disable-delay-time",&bklt_en_disable_delay_value);
	if (ret < 0)
	{
		bklt_en_disable_delay_value = 5;
	}
#endif
	return 0;
}

static const struct of_device_id pwm_backlight_of_match[] = {
	{ .compatible = "pwm-backlight" },
	{ }
};

MODULE_DEVICE_TABLE(of, pwm_backlight_of_match);
#else
static int pwm_backlight_parse_dt(struct device *dev,
				  struct platform_pwm_backlight_data *data)
{
	return -ENODEV;
}

static
int pwm_backlight_brightness_default(struct device *dev,
				     struct platform_pwm_backlight_data *data,
				     unsigned int period)
{
	return -ENODEV;
}
#endif

static bool pwm_backlight_is_linear(struct platform_pwm_backlight_data *data)
{
	unsigned int nlevels = data->max_brightness + 1;
	unsigned int min_val = data->levels[0];
	unsigned int max_val = data->levels[nlevels - 1];
	/*
	 * Multiplying by 128 means that even in pathological cases such
	 * as (max_val - min_val) == nlevels the error at max_val is less
	 * than 1%.
	 */
	unsigned int slope = (128 * (max_val - min_val)) / nlevels;
	unsigned int margin = (max_val - min_val) / 20; /* 5% */
	int i;

	for (i = 1; i < nlevels; i++) {
		unsigned int linear_value = min_val + ((i * slope) / 128);
		unsigned int delta = abs(linear_value - data->levels[i]);

		if (delta > margin)
			return false;
	}

	return true;
}

static int pwm_backlight_initial_power_state(const struct pwm_bl_data *pb)
{
	struct device_node *node = pb->dev->of_node;

	/* Not booted with device tree or no phandle link to the node */
	if (!node || !node->phandle)
		return FB_BLANK_UNBLANK;

	/*
	 * If the driver is probed from the device tree and there is a
	 * phandle link pointing to the backlight node, it is safe to
	 * assume that another driver will enable the backlight at the
	 * appropriate time. Therefore, if it is disabled, keep it so.
	 */

	/* if the enable GPIO is disabled, do not enable the backlight */
	if (pb->enable_gpio && gpiod_get_value_cansleep(pb->enable_gpio) == 0)
		return FB_BLANK_POWERDOWN;

	/* The regulator is disabled, do not enable the backlight */
	if (!regulator_is_enabled(pb->power_supply))
		return FB_BLANK_POWERDOWN;

	/* The PWM is disabled, keep it like this */
	if (!pwm_is_enabled(pb->pwm))
		return FB_BLANK_POWERDOWN;

	return FB_BLANK_UNBLANK;
}

static int pwm_backlight_probe(struct platform_device *pdev)
{
	struct platform_pwm_backlight_data *data = dev_get_platdata(&pdev->dev);
	struct platform_pwm_backlight_data defdata;
	struct backlight_properties props;
	struct backlight_device *bl;
	struct device_node *node = pdev->dev.of_node;
	struct pwm_bl_data *pb;
	struct pwm_state state;
	unsigned int i;
	int ret;

	if (!data) {
		ret = pwm_backlight_parse_dt(&pdev->dev, &defdata);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to find platform data\n");
			return ret;
		}

		data = &defdata;
	}

	if (data->init) {
		ret = data->init(&pdev->dev);
		if (ret < 0)
			return ret;
	}

	pb = devm_kzalloc(&pdev->dev, sizeof(*pb), GFP_KERNEL);
	if (!pb) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	pb->notify = data->notify;
	pb->notify_after = data->notify_after;
	pb->check_fb = data->check_fb;
	pb->exit = data->exit;
	pb->dev = &pdev->dev;
	pb->enabled = false;
	pb->post_pwm_on_delay = data->post_pwm_on_delay;
	pb->pwm_off_delay = data->pwm_off_delay;
	strcpy(pb->fb_id, data->fb_id);

	pb->enable_gpio = devm_gpiod_get_optional(&pdev->dev, "enable",
						  GPIOD_ASIS);
	if (IS_ERR(pb->enable_gpio)) {
		ret = PTR_ERR(pb->enable_gpio);
		goto err_alloc;
	}

	/*
	 * If the GPIO is not known to be already configured as output, that
	 * is, if gpiod_get_direction returns either 1 or -EINVAL, change the
	 * direction to output and set the GPIO as active.
	 * Do not force the GPIO to active when it was already output as it
	 * could cause backlight flickering or we would enable the backlight too
	 * early. Leave the decision of the initial backlight state for later.
	 */
	if (pb->enable_gpio &&
	    gpiod_get_direction(pb->enable_gpio) != 0)
		gpiod_direction_output(pb->enable_gpio, 1);

	pb->power_supply = devm_regulator_get(&pdev->dev, "power");
	if (IS_ERR(pb->power_supply)) {
		ret = PTR_ERR(pb->power_supply);
		goto err_alloc;
	}

	pb->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(pb->pwm) && PTR_ERR(pb->pwm) != -EPROBE_DEFER && !node) {
		dev_err(&pdev->dev, "unable to request PWM, trying legacy API\n");
		pb->legacy = true;
		pb->pwm = pwm_request(data->pwm_id, "pwm-backlight");
	}

	if (IS_ERR(pb->pwm)) {
		ret = PTR_ERR(pb->pwm);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "unable to request PWM\n");
		goto err_alloc;
	}

	dev_dbg(&pdev->dev, "got pwm for backlight\n");

	/* Sync up PWM state. */
	pwm_init_state(pb->pwm, &state);

	/*
	 * The DT case will set the pwm_period_ns field to 0 and store the
	 * period, parsed from the DT, in the PWM device. For the non-DT case,
	 * set the period from platform data if it has not already been set
	 * via the PWM lookup table.
	 */
	if (!state.period && (data->pwm_period_ns > 0))
		state.period = data->pwm_period_ns;

	ret = pwm_apply_state(pb->pwm, &state);
	if (ret) {
		dev_err(&pdev->dev, "failed to apply initial PWM state: %d\n",
			ret);
		goto err_alloc;
	}

	memset(&props, 0, sizeof(struct backlight_properties));

	if (data->levels) {
		pb->levels = data->levels;

		/*
		 * For the DT case, only when brightness levels is defined
		 * data->levels is filled. For the non-DT case, data->levels
		 * can come from platform data, however is not usual.
		 */
		for (i = 0; i <= data->max_brightness; i++)
			if (data->levels[i] > pb->scale)
				pb->scale = data->levels[i];

		if (pwm_backlight_is_linear(data))
			props.scale = BACKLIGHT_SCALE_LINEAR;
		else
			props.scale = BACKLIGHT_SCALE_NON_LINEAR;
	} else if (!data->max_brightness) {
		/*
		 * If no brightness levels are provided and max_brightness is
		 * not set, use the default brightness table. For the DT case,
		 * max_brightness is set to 0 when brightness levels is not
		 * specified. For the non-DT case, max_brightness is usually
		 * set to some value.
		 */

		/* Get the PWM period (in nanoseconds) */
		pwm_get_state(pb->pwm, &state);

		ret = pwm_backlight_brightness_default(&pdev->dev, data,
						       state.period);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"failed to setup default brightness table\n");
			goto err_alloc;
		}

		for (i = 0; i <= data->max_brightness; i++) {
			if (data->levels[i] > pb->scale)
				pb->scale = data->levels[i];

			pb->levels = data->levels;
		}

		props.scale = BACKLIGHT_SCALE_NON_LINEAR;
	} else {
		/*
		 * That only happens for the non-DT case, where platform data
		 * sets the max_brightness value.
		 */
		pb->scale = data->max_brightness;
	}

	pb->lth_brightness = data->lth_brightness * (div_u64(state.period,
				pb->scale));

	props.type = BACKLIGHT_RAW;
	props.max_brightness = data->max_brightness;
	bl = backlight_device_register(dev_name(&pdev->dev), &pdev->dev, pb,
				       &pwm_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		ret = PTR_ERR(bl);
		if (pb->legacy)
			pwm_free(pb->pwm);
		goto err_alloc;
	}

	if (data->dft_brightness > data->max_brightness) {
		dev_warn(&pdev->dev,
			 "invalid default brightness level: %u, using %u\n",
			 data->dft_brightness, data->max_brightness);
		data->dft_brightness = data->max_brightness;
	}

	bl->props.brightness = data->dft_brightness;
	bl->props.power = pwm_backlight_initial_power_state(pb);
#if defined(CONFIG_OF) && defined(CONFIG_ARCH_ADVANTECH) //&& !defined(CONFIG_FB_MXC_DISP_FRAMEWORK)
	/* Inorder to power off pwm backlight for SI test */
	if (!of_machine_is_compatible("fsl,imx8mp")) {
		bl->props.fb_blank = FB_BLANK_NORMAL;
	}

	printk(KERN_INFO "[LVDS Sequence] 0 Set to power off pwm backlight at first.\n");
#endif
	backlight_update_status(bl);

	platform_set_drvdata(pdev, bl);
	return 0;

err_alloc:
	if (data->exit)
		data->exit(&pdev->dev);
	return ret;
}

static int pwm_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	backlight_device_unregister(bl);
	pwm_backlight_power_off(pb);

	if (pb->exit)
		pb->exit(&pdev->dev);
	if (pb->legacy)
		pwm_free(pb->pwm);

	return 0;
}

static void pwm_backlight_shutdown(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	pwm_backlight_power_off(pb);
}

#ifdef CONFIG_PM_SLEEP
static int pwm_backlight_suspend(struct device *dev)
{
	struct backlight_device *bl = dev_get_drvdata(dev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	if (pb->notify)
		pb->notify(pb->dev, 0);

	pwm_backlight_power_off(pb);

	if (pb->notify_after)
		pb->notify_after(pb->dev, 0);

	return 0;
}

static int pwm_backlight_resume(struct device *dev)
{
	struct backlight_device *bl = dev_get_drvdata(dev);

	backlight_update_status(bl);

	return 0;
}
#endif

static const struct dev_pm_ops pwm_backlight_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = pwm_backlight_suspend,
	.resume = pwm_backlight_resume,
	.poweroff = pwm_backlight_suspend,
	.restore = pwm_backlight_resume,
#endif
};

static struct platform_driver pwm_backlight_driver = {
	.driver		= {
		.name		= "pwm-backlight",
		.pm		= &pwm_backlight_pm_ops,
		.of_match_table	= of_match_ptr(pwm_backlight_of_match),
	},
	.probe		= pwm_backlight_probe,
	.remove		= pwm_backlight_remove,
	.shutdown	= pwm_backlight_shutdown,
};

module_platform_driver(pwm_backlight_driver);

MODULE_DESCRIPTION("PWM based Backlight Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pwm-backlight");
