/* music_input_driver.c
 * Basic GPIO button driver for /dev/music_input
 * Creates a char device that blocks until a button interrupt occurs.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/mutex.h>

#define DRV_NAME "music_input"

static dev_t devno;
static struct cdev cdev_obj;
static struct class *cls;
static struct device *devnode;

static struct gpio_desc *btn_gpiod;
static int btn_irq = -1;

/* Readers sleep here until an interrupt wakes them */
static DECLARE_WAIT_QUEUE_HEAD(waitq);
static DEFINE_MUTEX(event_lock);
static int event_ready = 0;

static ssize_t mid_read(struct file *f, char __user *ubuf, size_t len, loff_t *off)
{
	char val = 'B';

	if (len < 1)
		return -EINVAL;

	wait_event_interruptible(waitq, event_ready != 0);

	mutex_lock(&event_lock);
	event_ready = 0;
	mutex_unlock(&event_lock);

	if (copy_to_user(ubuf, &val, 1))
		return -EFAULT;

	return 1;
}

static __poll_t mid_poll(struct file *f, struct poll_table_struct *pt)
{
	__poll_t mask = 0;

	poll_wait(f, &waitq, pt);

	if (event_ready)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.read  = mid_read,
	.poll  = mid_poll,
};

static irqreturn_t btn_isr(int irq, void *arg)
{
	mutex_lock(&event_lock);
	event_ready = 1;
	mutex_unlock(&event_lock);

	wake_up_interruptible(&waitq);
	return IRQ_HANDLED;
}

static int __init mid_init(void)
{
	int rc;

	/* Allocate char device number */
	rc = alloc_chrdev_region(&devno, 0, 1, DRV_NAME);
	if (rc)
		return rc;

	/* Register char device */
	cdev_init(&cdev_obj, &fops);
	rc = cdev_add(&cdev_obj, devno, 1);
	if (rc)
		goto err_cdev;

	/* Create /dev/music_input */
	cls = class_create(THIS_MODULE, DRV_NAME);
	if (IS_ERR(cls)) {
		rc = PTR_ERR(cls);
		goto err_class;
	}

	devnode = device_create(cls, NULL, devno, NULL, DRV_NAME);
	if (IS_ERR(devnode)) {
		rc = PTR_ERR(devnode);
		goto err_dev;
	}

	/* Request GPIO named "btn" from device tree */
	btn_gpiod = gpiod_get(devnode, "btn", GPIOD_IN);
	if (IS_ERR(btn_gpiod)) {
		rc = PTR_ERR(btn_gpiod);
		goto err_gpio;
	}

	rc = gpiod_direction_input(btn_gpiod);
	if (rc)
		goto err_irq;

	/* Convert to IRQ */
	btn_irq = gpiod_to_irq(btn_gpiod);
	if (btn_irq < 0) {
		rc = btn_irq;
		goto err_irq;
	}

	/* Register interrupt */
	rc = request_irq(btn_irq, btn_isr,
			 IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			 DRV_NAME, NULL);
	if (rc)
		goto err_irq;

	pr_info("music_input: driver loaded, device /dev/%s\n", DRV_NAME);
	return 0;

err_irq:
	if (!IS_ERR_OR_NULL(btn_gpiod))
		gpiod_put(btn_gpiod);
err_gpio:
	device_destroy(cls, devno);
err_dev:
	class_destroy(cls);
err_class:
	cdev_del(&cdev_obj);
err_cdev:
	unregister_chrdev_region(devno, 1);
	return rc;
}

static void __exit mid_exit(void)
{
	if (btn_irq >= 0)
		free_irq(btn_irq, NULL);

	if (!IS_ERR_OR_NULL(btn_gpiod))
		gpiod_put(btn_gpiod);

	device_destroy(cls, devno);
	class_destroy(cls);
	cdev_del(&cdev_obj);
	unregister_chrdev_region(devno, 1);

	pr_info("music_input: driver unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Prudhvi Raj Belide");
MODULE_DESCRIPTION("Simple GPIO button driver for /dev/music_input");

module_init(mid_init);
module_exit(mid_exit);

