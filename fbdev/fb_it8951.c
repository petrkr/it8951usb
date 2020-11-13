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

static DEFINE_IDR(sg_index_idr);
static DEFINE_RWLOCK(sg_index_lock); /* Also used to lock
							   file descriptor list for device */

typedef struct sg_device
{ /* holds the state of each scsi generic device */
  struct scsi_device *device;
  wait_queue_head_t open_wait; /* queue open() when O_EXCL present */
  struct mutex open_rel_lock;  /* held when in open() or release() */
  int sg_tablesize;            /* adapter's max scatter-gather table size */
  u32 index;                   /* device index number */
  struct list_head sfds;
  rwlock_t sfd_lock;  /* protect access to sfd list */
  atomic_t detaching; /* 0->device usable, 1->device detaching */
  bool exclude;       /* 1->open(O_EXCL) succeeded and is active */
  int open_cnt;       /* count of opens (perhaps < num(sfds) ) */
  char sgdebug;       /* 0->off, 1->sense, 9->dump dev, 10-> all devs */
  struct gendisk *disk;
  struct cdev *cdev; /* char_dev [sysfs: /sys/cdev/major/sg<n>] */
  struct kref d_ref;
} Sg_device;


static int sr_probe(struct device *);
static int sr_remove(struct device *);
static blk_status_t sr_init_command(struct scsi_cmnd *SCpnt);
static int sr_done(struct scsi_cmnd *);
static int sr_runtime_suspend(struct device *dev);

static struct scsi_driver sr_template = {
	.gendrv = {
		.name   	= "it8951_epaper",
		.owner		= THIS_MODULE,
		.probe		= sr_probe,
    .probe_type	= PROBE_FORCE_SYNCHRONOUS,
		.remove		= sr_remove,
	},
	.init_command		= sr_init_command,
	.done			= sr_done,
};

static int sr_probe(struct device *dev)
{
  printk("IT8951 Probe");
  struct scsi_device *sdev = to_scsi_device(dev);

  printk("  DEV type: %d", sdev->type);
  printk("  DEV Vendor: %s", sdev->vendor);
  printk("  DEV Model: %s", sdev->model);
  printk("  DEV ver: %s", sdev->rev);

  if (sdev->type != TYPE_DISK) {
    printk("  Not disk");
    return -ENODEV;
  }

  if (strncmp(sdev->vendor, "Generic ", 8) != 0)
  {
    printk(KERN_ALERT "  SCSI Vendor does not match\n");
    return -ENODEV;
  }

  if (strncmp(sdev->model, "Storage RamDisc ", 8) != 0)
  {
    printk(KERN_ALERT "  SCSI Product does not match\n");
    return -ENODEV;
  }

  if (strncmp(sdev->rev, "1.00", 4) != 0)
  {
    printk(KERN_ALERT "  SCSI Productver does not match\n");
    return -ENODEV;
  }


  printk("IT8951 Probe Done");

  return 0;
}

static int sr_done(struct scsi_cmnd *SCpnt)
{
  printk("IT8951 done");
  return 0;
}

static blk_status_t sr_init_command(struct scsi_cmnd *SCpnt)
{
  printk("IT8951 init cmd");
  return 0;
}

static int sr_remove(struct device *dev)
{
  printk("IT8951 remove");
  return 0;
}

/* SCSI Generic */
static int sg_add_device(struct device *, struct class_interface *);
static void sg_remove_device(struct device *, struct class_interface *);


static struct class_interface sg_interface = {
    .add_dev = sg_add_device,
    .remove_dev = sg_remove_device,
};


static Sg_device *
sg_alloc(struct gendisk *disk, struct scsi_device *scsidp)
{
  struct request_queue *q = scsidp->request_queue;
  Sg_device *sdp;
  unsigned long iflags;
  int error;
  u32 k;

  sdp = kzalloc(sizeof(Sg_device), GFP_KERNEL);
  if (!sdp)
  {
    sdev_printk(KERN_WARNING, scsidp, "%s: kmalloc Sg_device "
                                      "failure\n",
                __func__);
    return ERR_PTR(-ENOMEM);
  }

  idr_preload(GFP_KERNEL);
  write_lock_irqsave(&sg_index_lock, iflags);

  error = idr_alloc(&sg_index_idr, sdp, 0, SG_MAX_DEVS, GFP_NOWAIT);
  if (error < 0)
  {
    if (error == -ENOSPC)
    {
      sdev_printk(KERN_WARNING, scsidp,
                  "Unable to attach sg device type=%d, minor number exceeds %d\n",
                  scsidp->type, SG_MAX_DEVS - 1);
      error = -ENODEV;
    }
    else
    {
      sdev_printk(KERN_WARNING, scsidp, "%s: idr "
                                        "allocation Sg_device failure: %d\n",
                  __func__, error);
    }
    goto out_unlock;
  }
  k = error;

  sdev_printk(KERN_INFO, scsidp, "sg_alloc: dev=%d \n", k);
  sprintf(disk->disk_name, "it8951usb%d", k);
  disk->first_minor = k;
  sdp->disk = disk;
  sdp->device = scsidp;
  mutex_init(&sdp->open_rel_lock);
  INIT_LIST_HEAD(&sdp->sfds);
  init_waitqueue_head(&sdp->open_wait);
  atomic_set(&sdp->detaching, 0);
  rwlock_init(&sdp->sfd_lock);
  sdp->sg_tablesize = queue_max_segments(q);
  sdp->index = k;
  kref_init(&sdp->d_ref);
  error = 0;

out_unlock:
  write_unlock_irqrestore(&sg_index_lock, iflags);
  idr_preload_end();

  if (error)
  {
    kfree(sdp);
    return ERR_PTR(error);
  }
  return sdp;
}


static int
sg_add_device(struct device *dev, struct class_interface *intf)
{
  printk("Calling add device");
  struct scsi_device *scsidp = to_scsi_device(dev->parent);
  struct gendisk *disk;
  Sg_device *sdp = NULL;
  int error;

  printk("  DEV type: %d", scsidp->type);
  printk("  DEV Vendor: %s", scsidp->vendor);
  printk("  DEV Model: %s", scsidp->model);
  printk("  DEV ver: %s", scsidp->rev);

  return 0;
}

static void
sg_remove_device(struct device *dev, struct class_interface *intf)
{
  printk("Calling remove device");

  struct scsi_device *scsidp = to_scsi_device(dev->parent);
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
  
  //unregister_chrdev_region(MKDEV(51, 0), SG_MAX_DEVS);

  scsi_unregister_driver(&sr_template.gendrv);
  scsi_unregister_interface(&sg_interface);

	//unregister_blkdev(51, "it8951usb");

  printk(KERN_INFO "it8951usb module removed\n");
}

static int __init fb_it8951_init2(void)
{
  int rc = 0;

  printk(KERN_INFO "it8951usb module initializing...\n");

  //rc = alloc_chrdev_region(&base_dev, 0, SG_MAX_SEGMENTS, "it8951_");
  //rc = register_chrdev_region(MKDEV(51, 0), SG_MAX_DEVS, "it8951");
  //printk(KERN_INFO "  Register char dev RC: %d\n", rc);
  rc = scsi_register_interface(&sg_interface);
  //printk(KERN_INFO "  Register SCSI interface RC: %d\n", rc);
	//rc = register_blkdev(51, "it8951usb");
	//if (rc)
	//	return rc;
	rc = scsi_register_driver(&sr_template.gendrv);
  printk(KERN_INFO "  Register SCSI driver RC: %d\n", rc);
  //if (rc)
	//	unregister_blkdev(51, "it8951usb");

  printk(KERN_INFO "it8951usb module loaded\n");

  return rc;
}

module_init(fb_it8951_init2);
module_exit(fb_it8951_exit);
