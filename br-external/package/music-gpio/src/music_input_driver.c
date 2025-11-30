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
#define DEBOUNCE_MS 300
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


/* Encoder GPIOs */
static struct gpio_desc *encoder_clk_gpiod;
static struct gpio_desc *encoder_dt_gpiod;
static struct gpio_desc *encoder_sw_gpiod;
static int encoder_clk_irq = -1;
static int encoder_sw_irq = -1;

/* Encoder state */
static unsigned long last_encoder_time = 0;
static int last_clk_state = 1;

/* Event queue */
static char event_buffer[EVENT_BUF_SIZE];
static int buf_head = 0;
static int buf_tail = 0;
static DEFINE_SPINLOCK(buf_lock);
static DECLARE_WAIT_QUEUE_HEAD(read_wait);

/* Cloud Mode */
static struct gpio_desc *cloud_gpiod;
static int cloud_irq = -1;
static unsigned long last_cloud_time = 0;

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

static irqreturn_t cloud_isr(int irq, void *data)
{
    unsigned long now = jiffies;
    if (time_after(now, last_cloud_time + msecs_to_jiffies(DEBOUNCE_MS))) {
        last_cloud_time = now;
        pr_info("%s: CLOUD/LOCAL toggle pressed\n", DRV_NAME);
        queue_event('C');
    }
    return IRQ_HANDLED;
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


static irqreturn_t encoder_clk_isr(int irq, void *data)
{
    unsigned long now = jiffies;
    int clk_state, dt_state;

    /* 5 ms debounce for the rotary encoder */
    if (!time_after(now, last_encoder_time + msecs_to_jiffies(40)))
        return IRQ_HANDLED;

    last_encoder_time = now;

    /* Read the current pin states */
    clk_state = gpiod_get_value(encoder_clk_gpiod);
    dt_state  = gpiod_get_value(encoder_dt_gpiod);

    /*
     * We detect the FALLING edge on CLK.
     *
     * Because:
     *   - KY-040 is mechanically noisy
     *   - Falling edge decoding gives the cleanest directional signal
     *   - Active-low pins mean "0" = pressed/low level
     *
     * last_clk_state = 1 → high
     * clk_state      = 0 → falling edge (high → low)
     */
    if (last_clk_state == 1 && clk_state == 0) {

        /* Direction:
         * If DT == 1 → clockwise → volume up
         * If DT == 0 → counter-clockwise → volume down
         */
        if (dt_state) {
            pr_info("%s: VOLUME UP\n", DRV_NAME);
            queue_event('U');
        } else {
            pr_info("%s: VOLUME DOWN\n", DRV_NAME);
            queue_event('D');
        }
    }

    /* Remember the last CLK state */
    last_clk_state = clk_state;

    return IRQ_HANDLED;
}

static irqreturn_t encoder_sw_isr(int irq, void *data)
{
    unsigned long now = jiffies;
    static unsigned long last_sw_time = 0;
    
    if (time_after(now, last_sw_time + msecs_to_jiffies(DEBOUNCE_MS))) {
        last_sw_time = now;
        pr_info("%s: ENCODER BUTTON pressed\n", DRV_NAME);
        queue_event('M'); // M for Mute toggle
    }
    
    return IRQ_HANDLED;
}

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

    /* Setup play button */
    play_gpiod = devm_gpiod_get(dev, "play", GPIOD_IN);
    if (IS_ERR(play_gpiod)) {
        dev_err(dev, "Failed to get play-gpios\n");
        ret = PTR_ERR(play_gpiod);
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

    /* Setup next button */
    next_gpiod = devm_gpiod_get(dev, "next", GPIOD_IN);
    if (IS_ERR(next_gpiod)) {
        dev_err(dev, "Failed to get next-gpios\n");
        ret = PTR_ERR(next_gpiod);
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

    /* Setup prev button */
    prev_gpiod = devm_gpiod_get(dev, "prev", GPIOD_IN);
    if (IS_ERR(prev_gpiod)) {
        dev_err(dev, "Failed to get prev-gpios\n");
        ret = PTR_ERR(prev_gpiod);
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
    
    
    /* Setup cloud toggle button */
	cloud_gpiod = devm_gpiod_get(dev, "cloud", GPIOD_IN);
	if (IS_ERR(cloud_gpiod)) {
  	  dev_err(dev, "Failed to get cloud-gpios\n");
    	  ret = PTR_ERR(cloud_gpiod);
   	 goto err_gpio;
	}

	cloud_irq = gpiod_to_irq(cloud_gpiod);
	if (cloud_irq < 0) {
 	   dev_err(dev, "cloud gpiod_to_irq failed\n");
  	  ret = cloud_irq;
  	  goto err_gpio;
	}

	ret = devm_request_irq(dev, cloud_irq, cloud_isr,
                       IRQF_TRIGGER_FALLING,
                       "cloud_btn", NULL);
	if (ret) {
   	 dev_err(dev, "cloud request_irq failed\n");
   	 goto err_gpio;
	}

    /* Setup encoder CLK pin */
    encoder_clk_gpiod = devm_gpiod_get(dev, "encoder-clk", GPIOD_IN);
    if (IS_ERR(encoder_clk_gpiod)) {
        dev_err(dev, "Failed to get encoder-clk-gpios\n");
        ret = PTR_ERR(encoder_clk_gpiod);
        goto err_gpio;
    }

    encoder_clk_irq = gpiod_to_irq(encoder_clk_gpiod);
    if (encoder_clk_irq < 0) {
        dev_err(dev, "encoder clk gpiod_to_irq failed\n");
        ret = encoder_clk_irq;
        goto err_gpio;
    }

    ret = devm_request_irq(dev, encoder_clk_irq, encoder_clk_isr,
                           IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                           "encoder_clk", NULL);
    if (ret) {
        dev_err(dev, "encoder clk request_irq failed\n");
        goto err_gpio;
    }

    /* Setup encoder DT pin (no interrupt, just read state) */
    encoder_dt_gpiod = devm_gpiod_get(dev, "encoder-dt", GPIOD_IN);
    if (IS_ERR(encoder_dt_gpiod)) {
        dev_err(dev, "Failed to get encoder-dt-gpios\n");
        ret = PTR_ERR(encoder_dt_gpiod);
        goto err_gpio;
    }

    /* Setup encoder SW (button) pin */
    encoder_sw_gpiod = devm_gpiod_get(dev, "encoder-sw", GPIOD_IN);
    if (IS_ERR(encoder_sw_gpiod)) {
        dev_err(dev, "Failed to get encoder-sw-gpios\n");
        ret = PTR_ERR(encoder_sw_gpiod);
        goto err_gpio;
    }

    encoder_sw_irq = gpiod_to_irq(encoder_sw_gpiod);
    if (encoder_sw_irq < 0) {
        dev_err(dev, "encoder sw gpiod_to_irq failed\n");
        ret = encoder_sw_irq;
        goto err_gpio;
    }

    ret = devm_request_irq(dev, encoder_sw_irq, encoder_sw_isr,
                           IRQF_TRIGGER_FALLING,
                           "encoder_sw", NULL);
    if (ret) {
        dev_err(dev, "encoder sw request_irq failed\n");
        goto err_gpio;
    }

    /* Initialize encoder CLK state */
    last_clk_state = gpiod_get_value(encoder_clk_gpiod);

    platform_set_drvdata(pdev, mid_dev);

    dev_info(dev, "3-button + encoder driver with event queue loaded\n");
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

