# Makefile for building the kernel module by Buildroot's kernel build system.
# obj-m := music_input_driver.o

# obj-m := music_input_driver.o

# User-space test app
USERSPACE_APP := button_test

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules
	$(CC) button_test.c -o $(USERSPACE_APP)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install
	install -m 0755 $(USERSPACE_APP) $(DESTDIR)/usr/bin/

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean
	rm -f $(USERSPACE_APP)
