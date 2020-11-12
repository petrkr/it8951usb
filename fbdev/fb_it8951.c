// Base kernel module includes
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>

// FrameBuffer includes
//#include <linux/fb.h>

// DEV includes
#include <linux/fs.h>

// Chardev includes
#include <linux/cdev.h>


// SCSI includes
#include <scsi/scsi.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/sg.h>


#define SG_MAX_DEVS 32768

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Petr Kracik <petrkr@petrkr.net>");
MODULE_DESCRIPTION("Driver for 6 inch IT8951 based e-Paper");
MODULE_VERSION("0.01");

#define DEVICE_NAME "it8951usb"
#define EXAMPLE_MSG "Hello, World!\n"
#define MSG_BUFFER_LEN 15

/* File ops */

/* Prototypes for device functions */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

static int major_num;
static int device_open_count = 0;
static char msg_buffer[MSG_BUFFER_LEN];
static char *msg_ptr;

/* This structure points to all of the device functions */
static struct file_operations file_ops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release};


/* SCSI Generic */
static int sg_add_device(struct device *, struct class_interface *);
static void sg_remove_device(struct device *, struct class_interface *);


static struct class_interface sg_interface = {
    .add_dev = sg_add_device,
    .remove_dev = sg_remove_device,
};

static int
sg_add_device(struct device *cl_dev, struct class_interface *cl_intf)
{
 printk("Calling add device");
 return 0;
}


static void
sg_remove_device(struct device *cl_dev, struct class_interface *cl_intf)
{
  printk("Calling remove device");
  return 0;
}

/* When a process reads from our device, this gets called. */
static ssize_t device_read(struct file *flip, char *buffer, size_t len, loff_t *offset)
{
  printk(KERN_ALERT "This operation is not supported.\n");
  return -EINVAL;
}

char file_buf[20] = "Hello World\n";
/* Called when a process tries to write to our device */
static ssize_t device_write(struct file *flip, const char *buffer, size_t len, loff_t *offset)
{
  int count;
  count = len > sizeof(file_buf) ? sizeof(file_buf) : len;

  printk("file_operations.write called with count=%d, maxlen=%ld, off=%lld\n", count, len, *offset);

  *offset += count;
  return count;
}

/* Called when a process opens our device */
static int device_open(struct inode *inode, struct file *file)
{

  printk(KERN_INFO "file_operations.open called\n");
  /* If device is open, return busy */
  if (device_open_count)
  {
    return -EBUSY;
  }

  device_open_count++;
  try_module_get(THIS_MODULE);
  return 0;
}

/* Called when a process closes our device */
static int device_release(struct inode *inode, struct file *file)
{
  printk(KERN_INFO "file_operations.release called\n");
  /* Decrement the open counter and usage count. Without this, the module would not unload. */
  device_open_count--;
  module_put(THIS_MODULE);
  return 0;
}

static int __init fb_it8951_init(void)
{
  /* Fill buffer with our message */
  strncpy(msg_buffer, EXAMPLE_MSG, MSG_BUFFER_LEN);

  /* Set the msg_ptr to the buffer */
  msg_ptr = msg_buffer;

  /* Try to register character device */
  major_num = register_chrdev(0, DEVICE_NAME, &file_ops);

  if (major_num < 0)
  {
    printk(KERN_ALERT "Could not register device: %d\n", major_num);
    return major_num;
  }
  else
  {
    printk(KERN_INFO "it8951usb module loaded with device major number %d\n", major_num);
    return 0;
  }
}

dev_t base_dev = 0;

static void __exit fb_it8951_exit(void)
{
  /* Remember — we have to clean up after ourselves. Unregister the character device. */

  //unregister_chrdev(234, "it8951_");
  //unregister_chrdev_region(base_dev, SG_MAX_DEVS);
  scsi_unregister_interface(&sg_interface);

  unregister_chrdev_region(MKDEV(51, 0), SG_MAX_DEVS);

  printk(KERN_INFO "it8951usb module removed\n");
}

static int __init fb_it8951_init2(void)
{
  int rc;

  printk(KERN_INFO "it8951usb module initializing...\n");

  //rc = alloc_chrdev_region(&base_dev, 0, SG_MAX_SEGMENTS, "it8951_");
  rc = register_chrdev_region(MKDEV(51, 0), SG_MAX_DEVS, "it8951_");

  printk(KERN_INFO "  Register char dev RC: %d\n", rc);

  rc = scsi_register_interface(&sg_interface);

  printk(KERN_INFO "  Register SCSI interface RC: %d\n", rc);

  printk(KERN_INFO "it8951usb module loaded\n");
  return rc;
}

module_init(fb_it8951_init2);
module_exit(fb_it8951_exit);
