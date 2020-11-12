
#define MAX_TRANSFER 60 * 1024

// IT8951
typedef struct it8951_inquiry
{
  unsigned char dontcare[8];
  unsigned char vendor_id[8];
  unsigned char product_id[16];
  unsigned char product_ver[4];
} IT8951_inquiry;

typedef struct it8951_deviceinfo
{
  unsigned int uiStandardCmdNo;
  unsigned int uiExtendedCmdNo;
  unsigned int uiSignature;
  unsigned int uiVersion;
  unsigned int width;
  unsigned int height;
  unsigned int update_buffer_addr;
  unsigned int image_buffer_addr;
  unsigned int temperature_segment;
  unsigned int ui_mode;
  unsigned int frame_count[8];
  unsigned int buffer_count;
  unsigned int reserved[9];
  void *command_table;
} IT8951_deviceinfo;

typedef struct it8951_display_area
{
  u_int32_t address;
  u_int32_t wavemode;
  u_int32_t x;
  u_int32_t y;
  u_int32_t w;
  u_int32_t h;
  u_int32_t wait_ready;
} IT8951_display_area;

/*
static int memory_write(int fd, unsigned int addr, unsigned int length, char *data)
{
  unsigned char write_cmd[12] = {
      0xfe, 0x00,
      (addr >> 24) & 0xff,
      (addr >> 16) & 0xff,
      (addr >> 8) & 0xff,
      addr & 0xff,
      0x82,
      (length >> 8) & 0xff,
      length & 0xff,
      0x00, 0x00, 0x00};

  sg_io_hdr_t io_hdr;
  u_int8_t i = 0;
  for (i; i < 12; i += 4)
  {
    printk("%02X %02X %02X %02X\n", write_cmd[i], write_cmd[i + 1],
           write_cmd[i + 2], write_cmd[i + 3]);
  }
  printk("\n");

  memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
  io_hdr.interface_id = 'S';
  io_hdr.cmd_len = 12;
  io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
  io_hdr.dxfer_len = length;
  io_hdr.dxferp = data;
  io_hdr.cmdp = write_cmd;
  io_hdr.timeout = 10000;

  if (vfs_ioctl(fd, SG_IO, &io_hdr) < 0)
  {
    printk(KERN_ALERT "SG_IO memory write failed");
  }

  return 0;
}
static int display_area(int fd, u_int32_t addr, u_int32_t x, u_int32_t y, u_int32_t w, u_int32_t h, u_int32_t mode)
{
  unsigned char display_image_cmd[16] = {
      0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x94};

  printk("display area (0x%08x)\n", addr);
  IT8951_display_area area;
  memset(&area, 0, sizeof(IT8951_display_area));
  area.address = addr;
  area.x = x;
  area.y = y;
  area.w = w;
  area.h = h;
  area.wait_ready = 1;
  area.wavemode = mode;

  printk("Updating\n  Area X: 0x%08x\n  Area Y: 0x%08x\n  Area W: 0x%08x\n, Area H: 0x%08x\n", area.x, area.y, area.w, area.h);

  unsigned char *data_buffer = (unsigned char *)vmalloc(sizeof(IT8951_display_area));
  memcpy(data_buffer, &area, sizeof(IT8951_display_area));

  sg_io_hdr_t io_hdr;

  memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
  io_hdr.interface_id = 'S';
  io_hdr.cmd_len = 16;
  io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
  io_hdr.dxfer_len = sizeof(IT8951_display_area);
  io_hdr.dxferp = data_buffer;
  io_hdr.cmdp = display_image_cmd;
  io_hdr.timeout = 50;

  if (vfs_ioctl(fd, SG_IO, &io_hdr) < 0)
  {
    printk(KERN_ALERT "SG_IO display failed");
  }
  return 0;
}

static void update_region(int x, int y, int w, int h, int mode)
{
  const char *filename = "/dev/sda";

  int fd, to, res;
  fd = filp_open(filename, O_RDWR | O_NONBLOCK, 0);
  if (IS_ERR(fd))
  {
    printk(KERN_ALERT "Could not open scsi device");
    return;
  }

  res = vfs_ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &to);
  if (res < 0)
  {
    printk(KERN_ALERT "%s is not a SCSI device\n", filename);
    return;
  }

  unsigned char inquiry_cmd[6] = {0x12, 0, 0, 0, 0, 0};
  unsigned char inquiry_result[96];
  unsigned char deviceinfo_cmd[16] = {
      0xfe, 0x00,             // SCSI Customer command
      0x38, 0x39, 0x35, 0x31, // Chip signature
      0x80, 0x00,             // Get System Info
      0x01, 0x00, 0x02, 0x00  // Version
  };
  unsigned char deviceinfo_result[112];

  sg_io_hdr_t io_hdr;

  memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
  io_hdr.interface_id = 'S';
  io_hdr.cmd_len = 6;
  io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
  io_hdr.dxfer_len = 96;
  io_hdr.dxferp = inquiry_result;
  io_hdr.cmdp = inquiry_cmd;
  io_hdr.timeout = 100;

  if (vfs_ioctl(fd, SG_IO, &io_hdr) < 0)
  {
    printk(KERN_ALERT "SG_IO INQUIRY failed");
  }

  IT8951_inquiry *inquiry = (IT8951_inquiry *)inquiry_result;

  if (strncmp(inquiry->vendor_id, "Generic ", 8) != 0)
  {
    printk(KERN_ALERT "SCSI Vendor does not match\n");
    return;
  }
  if (strncmp(inquiry->product_id, "Storage RamDisc ", 8) != 0)
  {
    printk(KERN_ALERT "SCSI Product does not match\n");
    return;
  }
  if (strncmp(inquiry->product_ver, "1.00", 4) != 0)
  {
    printk(KERN_ALERT "SCSI Productver does not match\n");
    return;
  }

  memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
  io_hdr.interface_id = 'S';
  io_hdr.cmd_len = sizeof(deviceinfo_cmd);
  io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
  io_hdr.dxfer_len = 112;
  io_hdr.dxferp = deviceinfo_result;
  io_hdr.cmdp = deviceinfo_cmd;
  io_hdr.timeout = 10000;

  if (vfs_ioctl(fd, SG_IO, &io_hdr) < 0)
  {
    printk(KERN_ALERT "SG_IO device info failed");
    return;
  }

  IT8951_deviceinfo *deviceinfo = (IT8951_deviceinfo *)deviceinfo_result;

  int width = __bswap_32(deviceinfo->width);
  int height = __bswap_32(deviceinfo->height);

  printk("Found a %dx%d epaper display\n", width, height);

  int addr = __bswap_32(deviceinfo->image_buffer_addr);

  printk("Table version: 0x%08x\n", __bswap_32(deviceinfo->uiVersion));
  printk("Image buffer addr 0x%08x (0x%08x)\n", addr, __bswap_32(addr));
  printk("Update buffer addr 0x%08x\n", __bswap_32(deviceinfo->update_buffer_addr));
  printk("Temperature segment: %d\n", __bswap_32(deviceinfo->temperature_segment));
  printk("UI mode: %d\n", __bswap_32(deviceinfo->ui_mode));
  printk("Buffer count: 0x%08x\n", __bswap_32(deviceinfo->buffer_count));

  int size = w * h;
  unsigned char *image = (unsigned char *)vmalloc(size);

  printk("Filling buffer by dummy data (length %d)\n", size);
  memset(image, 0xFF, size);

  unsigned int offset = 0;
  unsigned int startoffset = x + (width * y);
  unsigned int lines = MAX_TRANSFER / w;
  printk("Start offset: %d\n", startoffset);
  while (offset < size)
  {
    if ((offset / w) + lines > h)
    {
      lines = h - (offset / w);
      printk("Apply some h-offset)\n");
    }
    printk("Sending %dx%d chunk to %d,%d (offset %d - lines %d)\n", w, lines, x, y + (offset / w), offset, lines);
    memory_write(fd, addr + offset + startoffset, lines * w, &image[offset]);

    offset += lines * w;
  }
  printk("Starting refresh\n");
  display_area(fd, addr, x, y, w, h, mode);
}/*
