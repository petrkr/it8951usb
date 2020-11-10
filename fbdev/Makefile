obj-m += fb_it8951.o

all:
	$(MAKE) -I /usr/include -C /lib/modules/$(shell uname -r)/build $(MAKE_OPTS) M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

