# IT8951 e-paper controller

This is a small utility to send images to an e-paper display using an IT8951
controller like the waveshare e-paper hat. Instead of using the i2c, i80 or spi
interface this uses the USB interface that is normally used with the
E-LINK TCON DEMO windows application.

The usb interface of the IT8951 controller shows up as an usb mass storage
device with no medium inserted (similar to SD card readers). The display is
controlled by sending customer-defined SCSI commands. For access to device
 you need add user to group `disk` or use sudo (NOT RECOMMENDED!!).

Actuall version works only with full-width images (for example 1024 per line)


## Building

```shell-session
$ mkdir build
$ cd build
$ cmake ..
$ make
```

## Make sure user is in disk group
```shell-session
$ id
```


## Usage

```shell-session
Clear the display
$ ./it8951 -c /dev/sdb 0 0 1024 758

Send an 8-bit grayscale image (full size 1024 x 758)
$ ./it8951 /dev/sdb 0 0 1024 758 < image.raw

Generate an image and display it
$ convert -background white -fill black \
  -font Ubuntu -pointsize 50 label:"$(date)" \
  -gravity Center -extent 1024x758 \
  -depth 8 gray: \
  | ./it8951 -d /dev/sdb 0 0 1024 758
```
