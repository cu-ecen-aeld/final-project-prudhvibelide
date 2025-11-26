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
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#define DRV_NAME "music_input"
#define DEBOUNCE_MS 200
#define EVENT_BUF_SIZE 32

static unsigned long last_play_time = 0;
static unsigned long last_next_time = 0;
static unsigned long last_prev_time = 0;

static dev_t devno;
static struct cdev mid_cdev;
static struct class *mid_class;
static struct device *mid_dev;

static struct gpio_desc *play_gpiod;
static struct gpio_desc *next_gpiod;
static struct gpio_desc *prev_gpiod;
static int play_irq = -1;
static int next_irq = -1;
static int prev_irq = -1;

/* Event queue */
static char event_buffer[EVENT_BUF_SIZE];
static int buf_head = 0;
static int buf_tail = 0;
static DEFINE_SPINLOCK(buf_lock);
static DECLARE_WAIT_QUEUE_HEAD(read_wait);

static void queue_event(char event)
{
    unsigned long flags;
    spin_lock_irqsave(&buf_lock, flags);
    
    int next = (buf_head + 1) % EVENT_BUF_SIZE;
    if (next != buf_tail) {
        event_buffer[buf_head] = event;
        buf_head = next;
        wake_up_interruptible(&read_wait);
    }
    
    spin_unlock_irqrestore(&buf_lock, flags);
}

static irqreturn_t play_isr(int irq, void *data)
{
    unsigned long now = jiffies;
    if (time_after(now, last_play_time + msecs_to_jiffies(DEBOUNCE_MS))) {
        last_play_time = now;
        pr_info("%s: PLAY/PAUSE button pressed\n", DRV_NAME);
        queue_event('P');
    }
    return IRQ_HANDLED;
}

static irqreturn_t next_isr(int irq, void *data)
{
    unsigned long now = jiffies;
    if (time_after(now, last_next_time + msecs_to_jiffies(DEBOUNCE_MS))) {
        last_next_time = now;
        pr_info("%s: NEXT button pressed\n", DRV_NAME);
        queue_event('N');
    }
    return IRQ_HANDLED;
}

static irqreturn_t prev_isr(int irq, void *data)
{
    unsigned long now = jiffies;
    if (time_after(now, last_prev_time + msecs_to_jiffies(DEBOUNCE_MS))) {
        last_prev_time = now;
        pr_info("%s: PREV button pressed\n", DRV_NAME);
        queue_event('R');
    }
    return IRQ_HANDLED;
}

static ssize_t mid_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    char event;
    unsigned long flags;
    
    if (len < 1)
        return -EINVAL;
    
    /* Block until event available */
    if (wait_event_interruptible(read_wait, buf_head != buf_tail))
        return -ERESTARTSYS;
    
    spin_lock_irqsave(&buf_lock, flags);
    event = event_buffer[buf_tail];
    buf_tail = (buf_tail + 1) % EVENT_BUF_SIZE;
    spin_unlock_irqrestore(&buf_lock, flags);
    
    if (copy_to_user(buf, &event, 1))
        return -EFAULT;
    
    return 1;
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

static int music_input_probe(struct platform_device *pdev)
{
    int ret;
    struct device *dev = &pdev->dev;

    pr_info("%s: probe called\n", DRV_NAME);

    ret = alloc_chrdev_region(&devno, 0, 1, DRV_NAME);
    if (ret) {
        dev_err(dev, "alloc_chrdev_region failed\n");
        return ret;
    }

    cdev_init(&mid_cdev, &mid_fops);
    ret = cdev_add(&mid_cdev, devno, 1);
    if (ret) {
        dev_err(dev, "cdev_add failed\n");
        goto err_cdev;
    }

    mid_class = class_create(DRV_NAME);
    if (IS_ERR(mid_class)) {
        dev_err(dev, "class_create failed\n");
        ret = PTR_ERR(mid_class);
        goto err_class;
    }

    mid_dev = device_create(mid_class, dev, devno, NULL, DRV_NAME);
    if (IS_ERR(mid_dev)) {
        dev_err(dev, "device_create failed\n");
        ret = PTR_ERR(mid_dev);
        goto err_dev;
    }

    play_gpiod = devm_gpiod_get(dev, "play", GPIOD_IN);
    if (IS_ERR(play_gpiod)) {
        dev_err(dev, "Failed to get play-gpios\n");
        ret = PTR_ERR(play_gpiod);
        goto err_gpio;
    }

    next_gpiod = devm_gpiod_get(dev, "next", GPIOD_IN);
    if (IS_ERR(next_gpiod)) {
        dev_err(dev, "Failed to get next-gpios\n");
        ret = PTR_ERR(next_gpiod);
        goto err_gpio;
    }

    prev_gpiod = devm_gpiod_get(dev, "prev", GPIOD_IN);
    if (IS_ERR(prev_gpiod)) {
        dev_err(dev, "Failed to get prev-gpios\n");
        ret = PTR_ERR(prev_gpiod);
        goto err_gpio;
    }

    play_irq = gpiod_to_irq(play_gpiod);
    if (play_irq < 0) {
        dev_err(dev, "play gpiod_to_irq failed\n");
        ret = play_irq;
        goto err_gpio;
    }
    ret = devm_request_irq(dev, play_irq, play_isr,
                           IRQF_TRIGGER_FALLING,
                           "play_btn", NULL);
    if (ret) {
        dev_err(dev, "play request_irq failed\n");
        goto err_gpio;
    }

    next_irq = gpiod_to_irq(next_gpiod);
    if (next_irq < 0) {
        dev_err(dev, "next gpiod_to_irq failed\n");
        ret = next_irq;
        goto err_gpio;
    }
    ret = devm_request_irq(dev, next_irq, next_isr,
                           IRQF_TRIGGER_FALLING,
                           "next_btn", NULL);
    if (ret) {
        dev_err(dev, "next request_irq failed\n");
        goto err_gpio;
    }

    prev_irq = gpiod_to_irq(prev_gpiod);
    if (prev_irq < 0) {
        dev_err(dev, "prev gpiod_to_irq failed\n");
        ret = prev_irq;
        goto err_gpio;
    }
    ret = devm_request_irq(dev, prev_irq, prev_isr,
                           IRQF_TRIGGER_FALLING,
                           "prev_btn", NULL);
    if (ret) {
        dev_err(dev, "prev request_irq failed\n");
        goto err_gpio;
    }

    platform_set_drvdata(pdev, mid_dev);

    dev_info(dev, "3-button driver with event queue loaded\n");
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

static int music_input_remove(struct platform_device *pdev)
{
    device_destroy(mid_class, devno);
    class_destroy(mid_class);
    cdev_del(&mid_cdev);
    unregister_chrdev_region(devno, 1);

    pr_info("%s: driver removed\n", DRV_NAME);
    return 0;
}

static const struct of_device_id music_input_of_match[] = {
    { .compatible = "music-input-device", },
    { }
};
MODULE_DEVICE_TABLE(of, music_input_of_match);

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
MODULE_AUTHOR("Prudhvi Raj Belide");
MODULE_DESCRIPTION("3-button music input with event queue");
MODULE_ALIAS("platform:music_input");
