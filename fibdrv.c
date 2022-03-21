#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>

// #include "big_num.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

typedef struct BigNum {
    unsigned long long lower, upper;
} BigNum;

static inline void addBigNum(BigNum *sum, BigNum x, BigNum y)
{
    sum->upper = x.upper + y.upper;
    if (y.lower > ~x.lower)
        sum->upper++;
    sum->lower = x.lower + y.lower;
}

static inline void rightShiftBigNum(BigNum *num)
{
    unsigned long long upper_rem = num->upper % 10;
    num->upper /= 10;
    unsigned long long lower_rem = num->lower % 10;
    unsigned long long max_num_64 = (unsigned long long) -1;
    num->lower = num->lower / 10 + upper_rem * (max_num_64 / 10) +
                 (lower_rem + upper_rem * 6) / 10;
}

static BigNum *fib_sequence(long long k)
{
    BigNum *first = vmalloc(sizeof(*first));
    first->upper = 0;
    first->lower = 0;
    BigNum *second = vmalloc(sizeof(*second));
    second->upper = 0;
    second->lower = 1;
    BigNum *next = vmalloc(sizeof(*next));
    next->upper = 0;
    next->lower = k;
    BigNum *temp = next;

    for (int i = 2; i <= k; i++) {
        addBigNum(next, *first, *second);
        temp = next;
        next = first;
        first = second;
        second = temp;
    }
    return temp;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    BigNum *num = fib_sequence(*offset);
    char str[45];
    int idx = 0;
    while (idx < 44) {
        if (num->upper != 0) {
            str[idx] = (num->lower % 10 + (num->upper % 10) * 6) % 10 + '0';
            rightShiftBigNum(num);
        } else {
            str[idx] = num->lower % 10 + '0';
            num->lower /= 10;
        }
        if (!num->lower && !num->upper)
            break;
        ++idx;
    }
    buf[idx + 1] = '\0';
    for (int j = idx; j >= 0; --j) {
        buf[j] = str[idx - j];
    }
    return (ssize_t) 1;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
