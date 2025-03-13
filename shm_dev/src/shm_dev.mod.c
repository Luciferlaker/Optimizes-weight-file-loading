#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

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
	{ 0x41be73b8, "module_layout" },
	{ 0xd498c45e, "class_unregister" },
	{ 0xe4afa4c, "device_destroy" },
	{ 0xedc4e20c, "wake_up_process" },
	{ 0x474e6470, "kthread_create_on_node" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x608741b5, "__init_swait_queue_head" },
	{ 0xb8b9f817, "kmalloc_order_trace" },
	{ 0x1c92a73e, "class_destroy" },
	{ 0x7fdb27c2, "device_create" },
	{ 0x6bc3fbc0, "__unregister_chrdev" },
	{ 0x7198fd91, "__class_create" },
	{ 0xe431ccc3, "__register_chrdev" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xd011c62f, "__pmd_alloc" },
	{ 0xf2bd355e, "__pud_alloc" },
	{ 0xaed69173, "__p4d_alloc" },
	{ 0xa9a24abe, "__pte_alloc" },
	{ 0xc512626a, "__supported_pte_mask" },
	{ 0x85efc7e0, "zero_pfn" },
	{ 0xa648e561, "__ubsan_handle_shift_out_of_bounds" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0xa6257a2f, "complete" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0x1000e51, "schedule" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0x40a0aafc, "__flush_tlb_all" },
	{ 0x9f4012e0, "__mmap_lock_do_trace_released" },
	{ 0xfb6c160e, "__mmap_lock_do_trace_start_locking" },
	{ 0x119fcb53, "__mmap_lock_do_trace_acquire_returned" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x72d79d83, "pgdir_shift" },
	{ 0xd2a3039e, "find_vma" },
	{ 0xf59e1343, "__tracepoint_mmap_lock_acquire_returned" },
	{ 0x668b19a1, "down_read" },
	{ 0x5c698a05, "__tracepoint_mmap_lock_start_locking" },
	{ 0x1344a3a2, "get_pid_task" },
	{ 0x619a0df6, "find_get_pid" },
	{ 0x800473f, "__cond_resched" },
	{ 0xb3f7646e, "kthread_should_stop" },
	{ 0x53b954a2, "up_read" },
	{ 0x57f7ae49, "__tracepoint_mmap_lock_released" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x1d19f77b, "physical_mask" },
	{ 0x9c4aa5db, "pv_ops" },
	{ 0xdad13544, "ptrs_per_p4d" },
	{ 0xa92ec74, "boot_cpu_data" },
	{ 0x25974000, "wait_for_completion" },
	{ 0x3eeb2322, "__wake_up" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x4c9d28b0, "phys_base" },
	{ 0x47da25b3, "remap_pfn_range" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0xbb9ed3bf, "mutex_trylock" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x92997ed8, "_printk" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "6548587A64449D74627467C");
