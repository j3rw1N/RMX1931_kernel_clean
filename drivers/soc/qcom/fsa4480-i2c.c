/* Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/soc/qcom/fsa4480-i2c.h>
#ifdef VENDOR_EDIT
/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/20,
 * Add for fsa4480 headset detection interrupt. */
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#endif /* VENDOR_EDIT */
#define FSA4480_I2C_NAME	"fsa4480-driver"

#define FSA4480_SWITCH_SETTINGS 0x04
#define FSA4480_SWITCH_CONTROL  0x05
#define FSA4480_SWITCH_STATUS1  0x07
#define FSA4480_SLOW_L          0x08
#define FSA4480_SLOW_R          0x09
#define FSA4480_SLOW_MIC        0x0A
#define FSA4480_SLOW_SENSE      0x0B
#define FSA4480_SLOW_GND        0x0C
#define FSA4480_DELAY_L_R       0x0D
#define FSA4480_DELAY_L_MIC     0x0E
#define FSA4480_DELAY_L_SENSE   0x0F
#define FSA4480_DELAY_L_AGND    0x10
#define FSA4480_RESET           0x1E
#ifdef VENDOR_EDIT
/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29,
 * Set current source to smallest 100uA */
#define FSA4480_SOURCE_SETTING  0x1F
#endif /* VENDOR_EDIT */

struct fsa4480_priv {
	struct regmap *regmap;
	struct device *dev;
	struct power_supply *usb_psy;
	struct notifier_block psy_nb;
	atomic_t usbc_mode;
	struct work_struct usbc_analog_work;
	struct blocking_notifier_head fsa4480_notifier;
	struct mutex notification_lock;
	#ifdef VENDOR_EDIT
	/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/20,
	* Add for fsa4480 headset detection interrupt. */
	s32 hs_det_pin;
	#endif /* VENDOR_EDIT */
};

struct fsa4480_reg_val {
	u16 reg;
	u8 val;
};

static const struct regmap_config fsa4480_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FSA4480_RESET,
};

static const struct fsa4480_reg_val fsa_reg_i2c_defaults[] = {
	#ifndef VENDOR_EDIT
	/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29,
	 * Set L/R slow turn on 2000us */
	{FSA4480_SLOW_L, 0x00},
	{FSA4480_SLOW_R, 0x00},
	#else
	{FSA4480_SLOW_L, 0x13},
	{FSA4480_SLOW_R, 0x13},
	#endif /* VENDOR_EDIT */
	{FSA4480_SLOW_MIC, 0x00},
	{FSA4480_SLOW_SENSE, 0x00},
	{FSA4480_SLOW_GND, 0x00},
	{FSA4480_DELAY_L_R, 0x00},
	{FSA4480_DELAY_L_MIC, 0x00},
	{FSA4480_DELAY_L_SENSE, 0x00},
	{FSA4480_DELAY_L_AGND, 0x09},
	{FSA4480_SWITCH_SETTINGS, 0x98},
	#ifdef VENDOR_EDIT
	/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29,
	 * Set current source to smallest 100uA */
	{FSA4480_SOURCE_SETTING, 0x01},
	#endif /* VENDOR_EDIT */
};

static void fsa4480_usbc_update_settings(struct fsa4480_priv *fsa_priv,
		u32 switch_control, u32 switch_enable)
{
	if (!fsa_priv->regmap) {
		dev_err(fsa_priv->dev, "%s: regmap invalid\n", __func__);
		return;
	}
	#ifdef VENDOR_EDIT
	/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29, Add log */
	dev_err(fsa_priv->dev, "%s: set reg(0x04) = 0x%x, reg(0x05) = 0x%x,\n",
		__func__, switch_enable, switch_control);
	#endif /* VENDOR_EDIT */

	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, 0x80);
	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_CONTROL, switch_control);
	/* FSA4480 chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, switch_enable);
}

static int fsa4480_usbc_event_changed(struct notifier_block *nb,
				      unsigned long evt, void *ptr)
{
	int ret;
	union power_supply_propval mode;
	struct fsa4480_priv *fsa_priv =
			container_of(nb, struct fsa4480_priv, psy_nb);
	struct device *dev;

	if (!fsa_priv)
		return -EINVAL;

	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

	if ((struct power_supply *)ptr != fsa_priv->usb_psy ||
				evt != PSY_EVENT_PROP_CHANGED)
		return 0;

	ret = power_supply_get_property(fsa_priv->usb_psy,
			POWER_SUPPLY_PROP_TYPEC_MODE, &mode);
	if (ret) {
		dev_err(dev, "%s: Unable to read USB TYPEC_MODE: %d\n",
			__func__, ret);
		return ret;
	}

	#ifndef VENDOR_EDIT
	/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29, Add log */
	dev_dbg(dev, "%s: USB change event received, supply mode %d, usbc mode %d, expected %d\n",
		__func__, mode.intval, fsa_priv->usbc_mode.counter,
		POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER);
	#else
	dev_err(dev, "%s: USB change event received, supply mode %d, usbc mode %d, expected %d\n",
		__func__, mode.intval, fsa_priv->usbc_mode.counter,
		POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER);
	#endif /* VENDOR_EDIT */

	switch (mode.intval) {
	case POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER:
	case POWER_SUPPLY_TYPEC_NONE:
		if (atomic_read(&(fsa_priv->usbc_mode)) == mode.intval)
			break; /* filter notifications received before */
		atomic_set(&(fsa_priv->usbc_mode), mode.intval);

		dev_dbg(dev, "%s: queueing usbc_analog_work\n",
			__func__);
		pm_stay_awake(fsa_priv->dev);
		schedule_work(&fsa_priv->usbc_analog_work);
		break;
	default:
		break;
	}
	return ret;
}

static int fsa4480_usbc_analog_setup_switches(struct fsa4480_priv *fsa_priv)
{
	int rc = 0;
	union power_supply_propval mode;
	struct device *dev;

	if (!fsa_priv)
		return -EINVAL;
	dev = fsa_priv->dev;
	if (!dev)
		return -EINVAL;

	mutex_lock(&fsa_priv->notification_lock);
	/* get latest mode again within locked context */
	rc = power_supply_get_property(fsa_priv->usb_psy,
			POWER_SUPPLY_PROP_TYPEC_MODE, &mode);
	if (rc) {
		dev_err(dev, "%s: Unable to read USB TYPEC_MODE: %d\n",
			__func__, rc);
		goto done;
	}
	dev_dbg(dev, "%s: setting GPIOs active = %d\n",
		__func__, mode.intval != POWER_SUPPLY_TYPEC_NONE);

	#ifdef VENDOR_EDIT
	/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29, Add log */
	dev_err(dev, "%s: USB mode %d\n", __func__, mode.intval);
	#endif /* VENDOR_EDIT */

	switch (mode.intval) {
	/* add all modes FSA should notify for in here */
	case POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER:
		#ifndef VENDOR_EDIT
		/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29 *
		 * Disconnect L and R to reduce POP */
		/* activate switches */
		fsa4480_usbc_update_settings(fsa_priv, 0x00, 0x9F);
		#else
		fsa4480_usbc_update_settings(fsa_priv, 0x00, 0x87);
		#endif /* VENDOR_EDIT */
		/* notify call chain on event */
		blocking_notifier_call_chain(&fsa_priv->fsa4480_notifier,
		mode.intval, NULL);
		#ifdef VENDOR_EDIT
		/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/20,
		* Add for fsa4480 headset detection interrupt. */
		gpio_direction_output(fsa_priv->hs_det_pin, 0);
		#endif /* VENDOR_EDIT */
		break;
	case POWER_SUPPLY_TYPEC_NONE:
		#ifdef VENDOR_EDIT
		/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/20,
		* Add for fsa4480 headset detection interrupt. */
		gpio_direction_output(fsa_priv->hs_det_pin, 1);
		#endif /* VENDOR_EDIT */
		/* notify call chain on event */
		blocking_notifier_call_chain(&fsa_priv->fsa4480_notifier,
				POWER_SUPPLY_TYPEC_NONE, NULL);

		/* deactivate switches */
		fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
		break;
	default:
		/* ignore other usb connection modes */
		break;
	}

done:
	mutex_unlock(&fsa_priv->notification_lock);
	return rc;
}

/*
 * fsa4480_reg_notifier - register notifier block with fsa driver
 *
 * @nb - notifier block of fsa4480
 * @node - phandle node to fsa4480 device
 *
 * Returns 0 on success, or error code
 */
int fsa4480_reg_notifier(struct notifier_block *nb,
			 struct device_node *node)
{
	int rc = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct fsa4480_priv *fsa_priv;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct fsa4480_priv *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;

	rc = blocking_notifier_chain_register
				(&fsa_priv->fsa4480_notifier, nb);
	if (rc)
		return rc;

	/*
	 * as part of the init sequence check if there is a connected
	 * USB C analog adapter
	 */
	dev_dbg(fsa_priv->dev, "%s: verify if USB adapter is already inserted\n",
		__func__);
	rc = fsa4480_usbc_analog_setup_switches(fsa_priv);

	return rc;
}
EXPORT_SYMBOL(fsa4480_reg_notifier);

/*
 * fsa4480_unreg_notifier - unregister notifier block with fsa driver
 *
 * @nb - notifier block of fsa4480
 * @node - phandle node to fsa4480 device
 *
 * Returns 0 on pass, or error code
 */
int fsa4480_unreg_notifier(struct notifier_block *nb,
			     struct device_node *node)
{
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct fsa4480_priv *fsa_priv;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct fsa4480_priv *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;

	fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
	return blocking_notifier_chain_unregister
					(&fsa_priv->fsa4480_notifier, nb);
}
EXPORT_SYMBOL(fsa4480_unreg_notifier);

static int fsa4480_validate_display_port_settings(struct fsa4480_priv *fsa_priv)
{
	u32 switch_status = 0;

	regmap_read(fsa_priv->regmap, FSA4480_SWITCH_STATUS1, &switch_status);

	if ((switch_status != 0x23) && (switch_status != 0x1C)) {
		pr_err("AUX SBU1/2 switch status is invalid = %u\n",
				switch_status);
		return -EIO;
	}

	return 0;
}
/*
 * fsa4480_switch_event - configure FSA switch position based on event
 *
 * @node - phandle node to fsa4480 device
 * @event - fsa_function enum
 *
 * Returns int on whether the switch happened or not
 */
int fsa4480_switch_event(struct device_node *node,
			 enum fsa_function event)
{
	int switch_control = 0;
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct fsa4480_priv *fsa_priv;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct fsa4480_priv *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;
	if (!fsa_priv->regmap)
		return -EINVAL;

	#ifdef VENDOR_EDIT
	/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29, Add log */
	pr_err("%s - switch event: %d\n", __func__, event);
	#endif /* VENDOR_EDIT */

	switch (event) {
	case FSA_MIC_GND_SWAP:
		#ifdef VENDOR_EDIT
		/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29 *
		 * Disconnect L and R to reduce POP */
		regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, 0x87);
		usleep_range(210, 215);
		#endif /* VENDOR_EDIT */
		regmap_read(fsa_priv->regmap, FSA4480_SWITCH_CONTROL,
				&switch_control);
		if ((switch_control & 0x07) == 0x07)
			switch_control = 0x0;
		else
			switch_control = 0x7;

		#ifndef VENDOR_EDIT
		/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29 *
		 * Disconnect L and R to reduce POP */
		fsa4480_usbc_update_settings(fsa_priv, switch_control, 0x9F);
		#else
		//swap Mic and Gnd, then connect L and R.
		regmap_write(fsa_priv->regmap, FSA4480_SWITCH_CONTROL, switch_control);
		usleep_range(110, 115);
		regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, 0x9F);
		#endif /* VENDOR_EDIT */
		break;
	case FSA_USBC_ORIENTATION_CC1:
		fsa4480_usbc_update_settings(fsa_priv, 0x18, 0xF8);
		return fsa4480_validate_display_port_settings(fsa_priv);
	case FSA_USBC_ORIENTATION_CC2:
		fsa4480_usbc_update_settings(fsa_priv, 0x78, 0xF8);
		return fsa4480_validate_display_port_settings(fsa_priv);
	case FSA_USBC_DISPLAYPORT_DISCONNECTED:
		fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL(fsa4480_switch_event);

#ifdef VENDOR_EDIT
/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/29 *
 * Control to connect L and R to reduce POP */
int fsa4480_set_LR_cnt(struct device_node *node, bool state)
{
	struct i2c_client *client = of_find_i2c_device_by_node(node);
	struct fsa4480_priv *fsa_priv;

	if (!client)
		return -EINVAL;

	fsa_priv = (struct fsa4480_priv *)i2c_get_clientdata(client);
	if (!fsa_priv)
		return -EINVAL;
	if (!fsa_priv->regmap)
		return -EINVAL;

	pr_err("%s - state = %d\n", __func__, state);
	if(true == state) {
		regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, 0x9F);
	} else {
		regmap_write(fsa_priv->regmap, FSA4480_SWITCH_SETTINGS, 0x87);
	}

	return 0;
}


EXPORT_SYMBOL(fsa4480_set_LR_cnt);
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/20,
* Add for fsa4480 headset detection interrupt. */
static int fsa4480_parse_dt(struct fsa4480_priv *fsa_priv,
	struct device *dev)
{
    struct device_node *dNode = dev->of_node;
    int ret;
    if (dNode == NULL)
        return -ENODEV;

	fsa_priv->hs_det_pin = of_get_named_gpio(dNode,
	        "fsa4480,hs-det-gpio", 0);
	if (fsa_priv->hs_det_pin < 0) {
	    pr_err("%s - get int error\n", __func__);
	    return -ENODEV;
	}
	ret = gpio_request(fsa_priv->hs_det_pin, "fsa4480_hs_det");
	gpio_direction_output(fsa_priv->hs_det_pin, 1);
	return ret;
}
#endif /* VENDOR_EDIT */

static void fsa4480_usbc_analog_work_fn(struct work_struct *work)
{
	struct fsa4480_priv *fsa_priv =
		container_of(work, struct fsa4480_priv, usbc_analog_work);

	if (!fsa_priv) {
		pr_err("%s: fsa container invalid\n", __func__);
		return;
	}
	fsa4480_usbc_analog_setup_switches(fsa_priv);
	pm_relax(fsa_priv->dev);
}

static void fsa4480_update_reg_defaults(struct regmap *regmap)
{
	u8 i;

	for (i = 0; i < ARRAY_SIZE(fsa_reg_i2c_defaults); i++)
		regmap_write(regmap, fsa_reg_i2c_defaults[i].reg,
				   fsa_reg_i2c_defaults[i].val);
}

static int fsa4480_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct fsa4480_priv *fsa_priv;
	int rc = 0;

	fsa_priv = devm_kzalloc(&i2c->dev, sizeof(*fsa_priv),
				GFP_KERNEL);
	if (!fsa_priv)
		return -ENOMEM;

	fsa_priv->dev = &i2c->dev;
	#ifdef VENDOR_EDIT
	/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/20,
	* Add for fsa4480 headset detection interrupt. */
	rc = fsa4480_parse_dt(fsa_priv, &i2c->dev);
	if (rc < 0) {
	    dev_err(fsa_priv->dev, "%s: of_node error: %d\n",
	            __func__, rc);
	    goto err_of_node;
	}
	#endif /* VENDOR_EDIT */

	fsa_priv->usb_psy = power_supply_get_by_name("usb");
	if (!fsa_priv->usb_psy) {
		rc = -EPROBE_DEFER;
		dev_dbg(fsa_priv->dev,
			"%s: could not get USB psy info: %d\n",
			__func__, rc);
		goto err_data;
	}

	fsa_priv->regmap = devm_regmap_init_i2c(i2c, &fsa4480_regmap_config);
	if (IS_ERR_OR_NULL(fsa_priv->regmap)) {
		dev_err(fsa_priv->dev, "%s: Failed to initialize regmap: %d\n",
			__func__, rc);
		if (!fsa_priv->regmap) {
			rc = -EINVAL;
			goto err_supply;
		}
		rc = PTR_ERR(fsa_priv->regmap);
		goto err_supply;
	}

	fsa4480_update_reg_defaults(fsa_priv->regmap);

	fsa_priv->psy_nb.notifier_call = fsa4480_usbc_event_changed;
	fsa_priv->psy_nb.priority = 0;
	rc = power_supply_reg_notifier(&fsa_priv->psy_nb);
	if (rc) {
		dev_err(fsa_priv->dev, "%s: power supply reg failed: %d\n",
			__func__, rc);
		goto err_supply;
	}

	mutex_init(&fsa_priv->notification_lock);
	i2c_set_clientdata(i2c, fsa_priv);

	INIT_WORK(&fsa_priv->usbc_analog_work,
		  fsa4480_usbc_analog_work_fn);

	fsa_priv->fsa4480_notifier.rwsem =
		(struct rw_semaphore)__RWSEM_INITIALIZER
		((fsa_priv->fsa4480_notifier).rwsem);
	fsa_priv->fsa4480_notifier.head = NULL;

	return 0;

err_supply:
	power_supply_put(fsa_priv->usb_psy);
err_data:
#ifdef VENDOR_EDIT
/* Zhao.Pan@PSW.MM.AudioDriver.HeadsetDet, 2019/05/20,
* Add for fsa4480 headset detection interrupt. */
    gpio_free(fsa_priv->hs_det_pin);
err_of_node:
#endif /* VENDOR_EDIT */
	devm_kfree(&i2c->dev, fsa_priv);
	return rc;
}

static int fsa4480_remove(struct i2c_client *i2c)
{
	struct fsa4480_priv *fsa_priv =
			(struct fsa4480_priv *)i2c_get_clientdata(i2c);

	if (!fsa_priv)
		return -EINVAL;

	fsa4480_usbc_update_settings(fsa_priv, 0x18, 0x98);
	cancel_work(&fsa_priv->usbc_analog_work);
	pm_relax(fsa_priv->dev);
	/* deregister from PMI */
	power_supply_unreg_notifier(&fsa_priv->psy_nb);
	power_supply_put(fsa_priv->usb_psy);
	mutex_destroy(&fsa_priv->notification_lock);
	dev_set_drvdata(&i2c->dev, NULL);

	return 0;
}

static const struct of_device_id fsa4480_i2c_dt_match[] = {
	{
		.compatible = "qcom,fsa4480-i2c",
	},
	{}
};

static struct i2c_driver fsa4480_i2c_driver = {
	.driver = {
		.name = FSA4480_I2C_NAME,
		.of_match_table = fsa4480_i2c_dt_match,
	},
	.probe = fsa4480_probe,
	.remove = fsa4480_remove,
};

static int __init fsa4480_init(void)
{
	int rc;

	rc = i2c_add_driver(&fsa4480_i2c_driver);
	if (rc)
		pr_err("fsa4480: Failed to register I2C driver: %d\n", rc);

	return rc;
}
module_init(fsa4480_init);

static void __exit fsa4480_exit(void)
{
	i2c_del_driver(&fsa4480_i2c_driver);
}
module_exit(fsa4480_exit);

MODULE_DESCRIPTION("FSA4480 I2C driver");
MODULE_LICENSE("GPL v2");
