/*
 *  lge/com_device/input/fsa8008.c
 *
 *  LGE 3.5 PI Headset detection driver using fsa8008.
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * Copyright (C) 2009 ~ 2010 LGE, Inc.
 * Author: Lee SungYoung <lsy@lge.com>
 *
 * Copyright (C) 2010 LGE, Inc.
 * Author: Kim Eun Hye <ehgrace.kim@lge.com>
 *
 * Copyright (C) 2011 LGE, Inc.
 * Author: Yoon Gi Souk <gisouk.yoon@lge.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* Interface is following;
 * source file is android/frameworks/base/services/java/com/android/server/HeadsetObserver.java
 * HEADSET_UEVENT_MATCH = "DEVPATH=/sys/devices/virtual/switch/h2w"
 * HEADSET_STATE_PATH = /sys/class/switch/h2w/state
 * HEADSET_NAME_PATH = /sys/class/switch/h2w/name
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/input.h>
#include <linux/debugfs.h>
#include <linux/wakelock.h>
#include <sound/fsa8008.h>

#define FSA8008_KEY_EN_DELAY_MS	(600 /* milli */ * HZ / 1000) /* convert milli to jiffies */
#define FSA8008_KEY_PRESS_TH_CNT 3
#define FSA8008_KEY_PRESS_DLY_MS (200 /* milli */ * HZ / 1000) /* convert milli to jiffies */
#define FSA8008_KEY_RELEASE_DLY_MS (200 /* milli */ * HZ / 1000)
#define FSA8008_DETECT_DELAY_MS	(20 /* milli */ * HZ / 1000) /* convert milli to jiffies */

static struct workqueue_struct *local_fsa8008_workqueue;

static struct wake_lock ear_hook_wake_lock;

struct hsd_info {
	/* function devices provided by this driver */
	struct switch_dev sdev;
	struct input_dev *input;

	/* mutex */
	struct mutex mutex_lock;

	/* h/w configuration : initilized by platform data */
	unsigned int gpio_detect; /* DET : to detect jack inserted or not */
	unsigned int gpio_mic_en; /* EN : to enable mic */
	unsigned int gpio_jpole;  /* JPOLE : 3pole or 4pole */
	unsigned int gpio_key;    /* S/E button */
	unsigned int gpio_key_cnt;/* S/E button threshold count */
	void (*set_headset_mic_bias)(int enable); /* callback function which is initialized while probing */

	unsigned int latency_for_detection;
	unsigned int latency_for_press_key;
	unsigned int latency_for_release_key;

	unsigned int key_code;

	/* irqs */
	unsigned int irq_detect;
	unsigned int irq_key;
	/* internal states */
	atomic_t irq_key_enabled;
	atomic_t is_3_pole_or_not;
	atomic_t btn_state;
	/* work for detect_work */
	struct delayed_work work_for_insert;
	struct delayed_work work_for_remove;
	struct delayed_work work_for_key_det_enable;
	struct delayed_work work_for_key_pressed;
	struct delayed_work work_for_key_released;
};

enum {
	NO_DEVICE   = 0,
	LGE_HEADSET = (1 << 0),
	LGE_HEADSET_NO_MIC = (1 << 1),
};

enum {
	FALSE = 0,
	TRUE = 1,
};

enum {
	BUTTON_RELEASED = 0,
	BUTTON_PRESSED = 1,
};

enum {
	EARJACK_INSERTED = 0,
	EARJACK_REMOVED = 1,
};

enum {
	EARJACK_TYPE_4_POLE = 0,
	EARJACK_TYPE_3_POLE = 1,
};

#ifdef CONFIG_MACH_MSM7X25A_V1
static bool s_bInitialized = false;
#endif

static ssize_t lge_hsd_print_name(struct switch_dev *sdev, char *buf)
{
	switch (switch_get_state(sdev)) {

	case NO_DEVICE:
		return sprintf(buf, "No Device");

	case LGE_HEADSET:
		return sprintf(buf, "Headset");

	case LGE_HEADSET_NO_MIC:
		return sprintf(buf, "Headset");
	}
	return -EINVAL;
}

static ssize_t lge_hsd_print_state(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "%d\n", switch_get_state(sdev));
}

static void button_pressed(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct hsd_info *hi = container_of(dwork, struct hsd_info, work_for_key_pressed);

	if (gpio_get_value_cansleep(hi->gpio_detect) == EARJACK_REMOVED
		|| (switch_get_state(&hi->sdev) != LGE_HEADSET)) {
		pr_debug("HSD.fsa8008: %s but ear jack is plugged out already! just ignore the event\n", __func__);
		return;
	}
	if (atomic_read(&hi->btn_state)) {
		pr_debug("HSD.fsa8008: %s but already pressed state!!\n", __func__);
		return;
	}

	pr_debug("HSD.fsa8008: %s [%d]\n", __func__, hi->gpio_key_cnt);

	atomic_set(&hi->btn_state, 1);
	input_report_key(hi->input, hi->key_code, 1);
	input_sync(hi->input);
}

static void button_released(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct hsd_info *hi = container_of(dwork, struct hsd_info, work_for_key_released);

	if (gpio_get_value_cansleep(hi->gpio_detect) == EARJACK_REMOVED
		|| (switch_get_state(&hi->sdev) != LGE_HEADSET)) {
		pr_debug("HSD.fsa8008: %s but ear jack is plugged out already! just ignore the event\n", __func__);
		return;
	}
	if(!atomic_read(&hi->btn_state)) {
		pr_debug("HSD.fsa8008: %s but already released state!!\n", __func__);
		return;
	}

	pr_debug("HSD.fsa8008: %s\n", __func__);

	atomic_set(&hi->btn_state, 0);
	hi->gpio_key_cnt = 0;
	input_report_key(hi->input, hi->key_code, 0);
	input_sync(hi->input);
}

static void button_enable(struct work_struct *work)
{
	int is_removed = 0;
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct hsd_info *hi = container_of(dwork, struct hsd_info, work_for_key_det_enable);

	is_removed = gpio_get_value_cansleep(hi->gpio_detect);

	if (!is_removed && !atomic_read(&hi->irq_key_enabled)) {
		unsigned long irq_flags;

		local_irq_save(irq_flags);
		enable_irq(hi->irq_key);
		local_irq_restore(irq_flags);
		atomic_set(&hi->irq_key_enabled, TRUE);

		atomic_set(&hi->btn_state, 0);
	}
}

static void insert_headset(struct work_struct *work)
{
#ifdef CONFIG_MACH_MSM7X25A_V1
	struct delayed_work *dwork = NULL;
	struct hsd_info *hi = NULL;
	int earjack_type = 0;
	int value = 0;

	/* LGE_CHANGE : bohyun.jung@lge.com
	 * prevent kernel panic in case that remove isr comes before probe is done. */
	if (!s_bInitialized) {
		pr_err("HSD.fsa8008: insert_headset but lge_hsd_probe() is not finished!!\n");
		return;
	}
	dwork = container_of(work, struct delayed_work, work);
	hi = container_of(dwork, struct hsd_info, work_for_insert);
	value = gpio_get_value_cansleep(hi->gpio_detect);
#else
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct hsd_info *hi = container_of(dwork, struct hsd_info, work_for_insert);
	int earjack_type;
	int value = gpio_get_value_cansleep(hi->gpio_detect);
#endif

	if(value != EARJACK_INSERTED) {
		pr_err("HSD.fsa8008: insert_headset but actually Fake inserted state!!\n");
		return;
	}

	pr_debug("HSD.fsa8008: %s\n", __func__);

	if (hi->set_headset_mic_bias)
		hi->set_headset_mic_bias(TRUE);

	gpio_set_value_cansleep(hi->gpio_mic_en, 1);

	msleep(hi->latency_for_detection); // 75 -> 10 ms

	earjack_type = gpio_get_value_cansleep(hi->gpio_jpole);

	if (earjack_type == EARJACK_TYPE_3_POLE) {
		pr_debug("HSD.fsa8008: 3 polarity earjack\n");

		if (hi->set_headset_mic_bias)
			hi->set_headset_mic_bias(FALSE);

		atomic_set(&hi->is_3_pole_or_not, 1);

		mutex_lock(&hi->mutex_lock);
		switch_set_state(&hi->sdev, LGE_HEADSET_NO_MIC);
		mutex_unlock(&hi->mutex_lock);

		input_report_switch(hi->input, SW_HEADPHONE_INSERT, 1);
		input_sync(hi->input);
	} else {
		pr_debug("HSD.fsa8008: 4 polarity earjack\n");

		atomic_set(&hi->is_3_pole_or_not, 0);

		cancel_delayed_work_sync(&(hi->work_for_key_det_enable));
		queue_delayed_work(local_fsa8008_workqueue, &(hi->work_for_key_det_enable), FSA8008_KEY_EN_DELAY_MS );

		mutex_lock(&hi->mutex_lock);
		switch_set_state(&hi->sdev, LGE_HEADSET);
		mutex_unlock(&hi->mutex_lock);

		input_report_switch(hi->input, SW_HEADPHONE_INSERT, 1);
		input_sync(hi->input); // 2012-07-01, donggyun.kim@lge.com - to prevent a lost uevent of earjack inserted
		input_report_switch(hi->input, SW_MICROPHONE_INSERT, 1);
		input_sync(hi->input);
	}
}

static void remove_headset(struct work_struct *work)
{
#ifdef CONFIG_MACH_MSM7X25A_V1
	struct delayed_work *dwork = NULL;
	struct hsd_info *hi = NULL;
	int has_mic = 0;
	int value = 0;

	/* LGE_CHANGE : bohyun.jung@lge.com
	 * prevent kernel panic in case that remove isr comes before probe is done. */
	if (!s_bInitialized) {
		pr_err("HSD.fsa8008: %s but lge_hsd_probe() is not finished!!\n", __func__);
		return;
	}

	dwork = container_of(work, struct delayed_work, work);
	hi = container_of(dwork, struct hsd_info, work_for_remove);
	has_mic = switch_get_state(&hi->sdev);
	value = gpio_get_value_cansleep(hi->gpio_detect);
#else
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct hsd_info *hi = container_of(dwork, struct hsd_info, work_for_remove);
	int has_mic = switch_get_state(&hi->sdev);

	int value = gpio_get_value_cansleep(hi->gpio_detect);
#endif

	if(value != EARJACK_REMOVED) {
		pr_err("HSD.fsa8008: %s but actually Fake removed state!!\n", __func__);
		return;
	} else {
		mutex_lock(&hi->mutex_lock);
		switch_set_state(&hi->sdev, NO_DEVICE);
		mutex_unlock(&hi->mutex_lock);
	}

	pr_debug("HSD.fsa8008: %s\n", __func__);

	gpio_set_value_cansleep(hi->gpio_mic_en, 0);

	if (hi->set_headset_mic_bias)
		hi->set_headset_mic_bias(FALSE);

	atomic_set(&hi->is_3_pole_or_not, 1);

	if (atomic_read(&hi->irq_key_enabled)) {
		unsigned long irq_flags;

		local_irq_save(irq_flags);
		disable_irq(hi->irq_key);
		local_irq_restore(irq_flags);
		atomic_set(&hi->irq_key_enabled, FALSE);
	}

	if (atomic_read(&hi->btn_state)) {
		input_report_key(hi->input, hi->key_code, 0);
		input_sync(hi->input);
		atomic_set(&hi->btn_state, 0);
	}

	input_report_switch(hi->input, SW_HEADPHONE_INSERT, 0);
	if (has_mic == LGE_HEADSET)
		input_report_switch(hi->input, SW_MICROPHONE_INSERT, 0);
	input_sync(hi->input);
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	struct hsd_info *hi = (struct hsd_info *) dev_id;

	int state = gpio_get_value_cansleep(hi->gpio_detect);

	wake_lock_timeout(&ear_hook_wake_lock, 2 * HZ);

	pr_debug("HSD.fsa8008: %s [%d]\n", __func__, state);

	if (state == EARJACK_REMOVED) {
		pr_debug("HSD.fsa8008: LGE headset removing\n");
		cancel_delayed_work_sync(&(hi->work_for_insert));
		queue_delayed_work(local_fsa8008_workqueue, &(hi->work_for_remove), FSA8008_DETECT_DELAY_MS );
	} else {
		pr_debug("HSD.fsa8008: LGE headset inserting\n");
		queue_delayed_work(local_fsa8008_workqueue, &(hi->work_for_insert), FSA8008_DETECT_DELAY_MS );
	}

	return IRQ_HANDLED;
}

static irqreturn_t button_irq_handler(int irq, void *dev_id)
{
	struct hsd_info *hi = (struct hsd_info *) dev_id;

	int value;

	wake_lock_timeout(&ear_hook_wake_lock, 2 * HZ);

	value = gpio_get_value_cansleep(hi->gpio_key);
	pr_debug("HSD.fsa8008: %s [%d]\n", __func__, value);

	if (value == BUTTON_PRESSED) {
		hi->gpio_key_cnt = 0;
		queue_delayed_work(local_fsa8008_workqueue, &(hi->work_for_key_pressed), hi->latency_for_press_key );
	} else
		queue_delayed_work(local_fsa8008_workqueue, &(hi->work_for_key_released), hi->latency_for_release_key );

	return IRQ_HANDLED;
}

static int lge_hsd_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct fsa8008_platform_data *pdata = pdev->dev.platform_data;

	struct hsd_info *hi;

	pr_debug("HSD.fsa8008: %s\n", __func__);

	hi = kzalloc(sizeof(struct hsd_info), GFP_KERNEL);

	if (NULL == hi) {
		pr_err("HSD.fsa8008: Failed to alllocate headset per device info\n");
		return -ENOMEM;
	}

	hi->key_code = pdata->key_code;

	platform_set_drvdata(pdev, hi);

	atomic_set(&hi->btn_state, 0);
	atomic_set(&hi->is_3_pole_or_not, 1);

	hi->gpio_detect = pdata->gpio_detect;
	hi->gpio_mic_en = pdata->gpio_mic_en;
	hi->gpio_jpole = pdata->gpio_jpole;
	hi->gpio_key = pdata->gpio_key;
	hi->set_headset_mic_bias = pdata->set_headset_mic_bias;

	hi->latency_for_detection = pdata->latency_for_detection;
	hi->latency_for_press_key = FSA8008_KEY_PRESS_DLY_MS;
	hi->latency_for_release_key = FSA8008_KEY_RELEASE_DLY_MS;
	hi->gpio_key_cnt = 0;
	INIT_DELAYED_WORK(&hi->work_for_insert, insert_headset);
	INIT_DELAYED_WORK(&hi->work_for_remove, remove_headset);
	INIT_DELAYED_WORK(&hi->work_for_key_det_enable, button_enable);
	mutex_init(&hi->mutex_lock);
	INIT_DELAYED_WORK(&hi->work_for_key_pressed, button_pressed);
	INIT_DELAYED_WORK(&hi->work_for_key_released, button_released);

	/* initialize gpio_detect */
	ret = gpio_request(hi->gpio_detect, "gpio_detect");
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to configure gpio%d (gpio_detect) gpio_request\n", hi->gpio_detect);
		goto error_01;
	}

	ret = gpio_direction_input(hi->gpio_detect);
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to configure gpio%d (gpio_detect) gpio_direction_input\n", hi->gpio_detect);
		goto error_02;
	}

	/* initialize gpio_jpole */
	ret = gpio_request(hi->gpio_jpole, "gpio_jpole");
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to configure gpio%d (gpio_jpole) gpio_request\n", hi->gpio_jpole);
		goto error_02;
	}

	ret = gpio_direction_input(hi->gpio_jpole);
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to configure gpio%d (gpio_jpole) gpio_direction_input\n", hi->gpio_jpole);
		goto error_03;
	}

	/* initialize gpio_key */
	ret = gpio_request(hi->gpio_key, "gpio_key");
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to configure gpio%d (gpio_key) gpio_request\n", hi->gpio_key);
		goto error_03;
	}

	ret = gpio_direction_input(hi->gpio_key);
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to configure gpio%d (gpio_key) gpio_direction_input\n", hi->gpio_key);
		goto error_04;
	}

	/* initialize gpio_mic_en */
	ret = gpio_request(hi->gpio_mic_en, "gpio_mic_en");
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to configure gpio%d (gpio_mic_en) gpio_request\n", hi->gpio_mic_en);
		goto error_04;
	}

	ret = gpio_direction_output(hi->gpio_mic_en, 0);
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to configure gpio%d (gpio_mic_en) gpio_direction_output\n", hi->gpio_mic_en);
		goto error_05;
	}

	/* initialize irq of gpio_jpole */
	hi->irq_detect = gpio_to_irq(hi->gpio_detect);

	pr_debug("HSD.fsa8008: hi->irq_detect = %d\n", hi->irq_detect);

	if (hi->irq_detect < 0) {
		pr_err("HSD.fsa8008: Failed to get interrupt number\n");
		ret = hi->irq_detect;
		goto error_05;
	}

	ret = request_threaded_irq(hi->irq_detect, NULL, gpio_irq_handler,
					IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, pdev->name, hi);

	if (ret) {
		pr_err("HSD.fsa8008: Failed to request button irq\n");
		goto error_05;
	}

	ret = irq_set_irq_wake(hi->irq_detect, 1);
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to set irq_detect interrupt wake\n");
		goto error_06;
	}

	/* initialize irq of gpio_key */
	hi->irq_key = gpio_to_irq(hi->gpio_key);

	pr_debug("HSD.fsa8008: hi->irq_key = %d\n", hi->irq_key);

	if (hi->irq_key < 0) {
		pr_err("HSD.fsa8008: Failed to get interrupt number\n");
		ret = hi->irq_key;
		goto error_06;
	}

	pr_debug("HSD.fsa8008: initialized irq of gpio_key\n");

	/* initialize switch device */
	hi->sdev.name = pdata->switch_name;
	hi->sdev.print_state = lge_hsd_print_state;
	hi->sdev.print_name = lge_hsd_print_name;

	pr_debug("HSD.fsa8008: name: %s state: %p name: %p\n", hi->sdev.name, hi->sdev.print_state, hi->sdev.print_name );
	ret = switch_dev_register(&hi->sdev);
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to register switch device %d\n", ret);
		goto error_07;
	}
	pr_debug("HSD.fsa8008: request_threaded_irq");
	ret = request_threaded_irq(hi->irq_key, NULL, button_irq_handler,
					IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, pdev->name, hi);

	if (ret) {
		pr_err("HSD.fsa8008: Failed to request button irq");
		goto error_06;
	}

	disable_irq(hi->irq_key);

	pr_debug("HSD.fsa8008: irq_set_irq_wake(hi->irq_key, 1)");
	ret = irq_set_irq_wake(hi->irq_key, 1);
	if (ret < 0) {
		pr_err("HSD.fsa8008: Failed to set irq_key interrupt wake\n");
		goto error_07;
	}
	pr_debug("HSD.fsa8008: initialize input device");

	/* initialize input device */
	hi->input = input_allocate_device();
	if (!hi->input) {
		pr_err("HSD.fsa8008: Failed to allocate input device\n");
		ret = -ENOMEM;
		goto error_08;
	}

	hi->input->name = pdata->keypad_name;

	hi->input->id.vendor    = 0x0001;
	hi->input->id.product   = 1;
	hi->input->id.version   = 1;

	/*input_set_capability(hi->input, EV_SW, SW_HEADPHONE_INSERT);*/
	set_bit(EV_SYN, hi->input->evbit);
	set_bit(EV_KEY, hi->input->evbit);
	set_bit(EV_SW, hi->input->evbit);
	set_bit(hi->key_code, hi->input->keybit);
	set_bit(SW_HEADPHONE_INSERT, hi->input->swbit);
	set_bit(SW_MICROPHONE_INSERT, hi->input->swbit);

	ret = input_register_device(hi->input);
	if (ret) {
		pr_err("HSD.fsa8008: Failed to register input device\n");
		goto error_09;
	}
	if (gpio_get_value_cansleep(hi->gpio_detect) == EARJACK_INSERTED) {
		queue_delayed_work(local_fsa8008_workqueue, &(hi->work_for_insert), 0); /* to detect in initialization with eacjack insertion */
	}

#ifdef AT_TEST_GPKD
	err = device_create_file(&pdev->dev, &dev_attr_hookkeylog);
#endif

#ifdef CONFIG_MACH_MSM7X25A_V1
	/* LGE_CHANGE : bohyun.jung@lge.com
	 * prevent kernel panic in case that remove isr comes before probe is done. */
	pr_debug("HSD.fsa8008: %s is done\n", __func__);
	s_bInitialized = true;
#endif

	return ret;

error_09:
	input_free_device(hi->input);
error_08:
	switch_dev_unregister(&hi->sdev);

error_07:
	free_irq(hi->irq_key, 0);
error_06:
	free_irq(hi->irq_detect, 0);

error_05:
	gpio_free(hi->gpio_mic_en);
error_04:
	gpio_free(hi->gpio_key);
error_03:
	gpio_free(hi->gpio_jpole);
error_02:
	gpio_free(hi->gpio_detect);

error_01:
	mutex_destroy(&hi->mutex_lock);
	kfree(hi);

	return ret;
}

static int lge_hsd_remove(struct platform_device *pdev)
{
	struct hsd_info *hi = (struct hsd_info *)platform_get_drvdata(pdev);

	pr_debug("HSD.fsa8008: %s\n", __func__);

	if (switch_get_state(&hi->sdev))
		queue_delayed_work(local_fsa8008_workqueue, &(hi->work_for_remove), 0 );

	input_unregister_device(hi->input);
	switch_dev_unregister(&hi->sdev);

	free_irq(hi->irq_key, 0);
	free_irq(hi->irq_detect, 0);

	gpio_free(hi->gpio_mic_en);
	gpio_free(hi->gpio_key);
	gpio_free(hi->gpio_jpole);
	gpio_free(hi->gpio_detect);

	mutex_destroy(&hi->mutex_lock);

	kfree(hi);

	return 0;
}

static struct platform_driver lge_hsd_driver = {
	.probe          = lge_hsd_probe,
	.remove         = lge_hsd_remove,
	.driver         = {
		.name   = "fsa8008",
		.owner  = THIS_MODULE,
	},
};

static int __init lge_hsd_init(void)
{
	int ret;

	pr_debug("HSD.fsa8008: %s\n", __func__);

	wake_lock_init(&ear_hook_wake_lock, WAKE_LOCK_SUSPEND, "ear_hook");
	pr_debug("HSD.fsa8008: wake lock init\n");

	local_fsa8008_workqueue = create_workqueue("fsa8008") ;
	if(!local_fsa8008_workqueue)
		return -ENOMEM;

	ret = platform_driver_register(&lge_hsd_driver);
	if (ret)
		pr_err("HSD.fsa8008: Failed to register platform driver\n");

	return ret;
}

static void __exit lge_hsd_exit(void)
{
	if(local_fsa8008_workqueue)
		destroy_workqueue(local_fsa8008_workqueue);
	local_fsa8008_workqueue = NULL;

	platform_driver_unregister(&lge_hsd_driver);
	pr_debug("HSD.fsa8008: %s\n", __func__);
	wake_lock_destroy(&ear_hook_wake_lock);
}

/* to make init after pmic8058-othc module */
/* module_init(lge_hsd_init); */
late_initcall_sync(lge_hsd_init);
module_exit(lge_hsd_exit);

MODULE_AUTHOR("Yoon Gi Souk <gisouk.yoon@lge.com>");
MODULE_DESCRIPTION("LGE Headset detection driver (fsa8008)");
MODULE_LICENSE("GPL");
