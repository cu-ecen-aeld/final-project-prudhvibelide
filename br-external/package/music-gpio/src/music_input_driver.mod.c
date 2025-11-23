#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x851a010f, "__platform_driver_register" },
	{ 0x69a20af4, "device_destroy" },
	{ 0x15b3ce7d, "class_destroy" },
	{ 0x17e56a8c, "cdev_del" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x122c3a7e, "_printk" },
	{ 0x619cb7dd, "simple_read_from_buffer" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xed44a979, "cdev_init" },
	{ 0x411ffbe0, "cdev_add" },
	{ 0xb0c6c225, "class_create" },
	{ 0xfeae87a5, "device_create" },
	{ 0xb7c4d2ac, "devm_gpiod_get" },
	{ 0xad5d5c8a, "gpiod_to_irq" },
	{ 0x262aaa36, "devm_request_threaded_irq" },
	{ 0x8b25c8cc, "_dev_info" },
	{ 0x13a0a82f, "_dev_err" },
	{ 0x83b61d90, "platform_driver_unregister" },
	{ 0x6f6ab014, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cmusic-input-device");
MODULE_ALIAS("of:N*T*Cmusic-input-deviceC*");

MODULE_INFO(srcversion, "D765DA25177A593961BA257");
