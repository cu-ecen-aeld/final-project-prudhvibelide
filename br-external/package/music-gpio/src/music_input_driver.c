#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/uaccess.h>

#define DRV_NAME "music_input"

static dev_t devno;
static struct cdev mid_cdev;
static struct class *mid_class;
static struct device *mid_dev;

static struct gpio_desc *btn_gpiod;
static int btn_irq = -1;

/* ---------------------------------------------------------------------- */
/* ISR – simple interrupt handler                                         */
/* ---------------------------------------------------------------------- */
static irqreturn_t mid_isr(int irq, void *data)
{
    pr_info("%s: Button interrupt occurred\n", DRV_NAME);
    return IRQ_HANDLED;
}

/* ---------------------------------------------------------------------- */
/* File operations                                                        */
/* ---------------------------------------------------------------------- */

static ssize_t mid_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    char msg[] = "button-read\n";
    return simple_read_from_buffer(buf, len, off, msg, sizeof(msg));
}

static int mid_open(struct inode *inode, struct file *f)
{
    return 0;
}

static int mid_release(struct inode *inode, struct file *f)
{
    return 0;
}

static const struct file_operations mid_fops = {
    .owner = THIS_MODULE,
    .open = mid_open,
    .release = mid_release,
    .read = mid_read,
};

/* ---------------------------------------------------------------------- */
/* Platform Driver Probe                                                  */
/* ---------------------------------------------------------------------- */

static int music_input_probe(struct platform_device *pdev)
{
    int ret;
    struct device *dev = &pdev->dev;

    pr_info("%s: probe called\n", DRV_NAME);

    /* Allocate char device */
    ret = alloc_chrdev_region(&devno, 0, 1, DRV_NAME);
    if (ret) {
        dev_err(dev, "alloc_chrdev_region failed\n");
        return ret;
    }

    /* Setup cdev */
    cdev_init(&mid_cdev, &mid_fops);
    ret = cdev_add(&mid_cdev, devno, 1);
    if (ret) {
        dev_err(dev, "cdev_add failed\n");
        goto err_cdev;
    }

    /* Create /sys/class/music_input */
    mid_class = class_create(DRV_NAME);
    if (IS_ERR(mid_class)) {
        dev_err(dev, "class_create failed\n");
        ret = PTR_ERR(mid_class);
        goto err_class;
    }

    /* Create /dev/music_input */
    mid_dev = device_create(mid_class, dev, devno, NULL, DRV_NAME);
    if (IS_ERR(mid_dev)) {
        dev_err(dev, "device_create failed\n");
        ret = PTR_ERR(mid_dev);
        goto err_dev;
    }

    /* Get GPIO from device tree using platform device */
    btn_gpiod = devm_gpiod_get(dev, "btn", GPIOD_IN);
    if (IS_ERR(btn_gpiod)) {
        dev_err(dev, "Failed to get btn-gpios from device tree\n");
        ret = PTR_ERR(btn_gpiod);
        goto err_gpio;
    }

    /* Convert GPIO → IRQ */
    btn_irq = gpiod_to_irq(btn_gpiod);
    if (btn_irq < 0) {
        dev_err(dev, "gpiod_to_irq failed\n");
        ret = btn_irq;
        goto err_gpio;
    }

    /* Request falling-edge interrupt */
    ret = devm_request_irq(dev, btn_irq, mid_isr,
                           IRQF_TRIGGER_FALLING,
                           DRV_NAME, NULL);
    if (ret) {
        dev_err(dev, "request_irq failed\n");
        goto err_gpio;
    }

    /* Store platform device pointer for cleanup */
    platform_set_drvdata(pdev, mid_dev);

    dev_info(dev, "driver probed successfully\n");
    return 0;

err_gpio:
    device_destroy(mid_class, devno);
err_dev:
    class_destroy(mid_class);
err_class:
    cdev_del(&mid_cdev);
err_cdev:
    unregister_chrdev_region(devno, 1);
    return ret;
}

/* ---------------------------------------------------------------------- */
/* Platform Driver Remove                                                 */
/* ---------------------------------------------------------------------- */

static int music_input_remove(struct platform_device *pdev)
{
    /* devm_* functions auto-cleanup GPIO and IRQ */
    device_destroy(mid_class, devno);
    class_destroy(mid_class);
    cdev_del(&mid_cdev);
    unregister_chrdev_region(devno, 1);

    pr_info("%s: driver removed\n", DRV_NAME);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Device Tree Matching                                                   */
/* ---------------------------------------------------------------------- */

static const struct of_device_id music_input_of_match[] = {
    { .compatible = "music-input-device", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, music_input_of_match);

/* ---------------------------------------------------------------------- */
/* Platform Driver Structure                                              */
/* ---------------------------------------------------------------------- */

static struct platform_driver music_input_driver = {
    .probe = music_input_probe,
    .remove = music_input_remove,
    .driver = {
        .name = DRV_NAME,
        .of_match_table = music_input_of_match,
    },
};

module_platform_driver(music_input_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Prudhvi");
MODULE_DESCRIPTION("Music button input driver - Platform Driver");
MODULE_ALIAS("platform:music_input");
