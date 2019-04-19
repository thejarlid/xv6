#pragma once

#include <multiboot.h>

#define MULTIBOOT2_HEADER_MAGIC			0xe85250d6
#define MULTIBOOT2_BOOTLOADER_MAGIC		0x36d76289

#define MULTIBOOT_HEADER_TAG_END			0
#define MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST	1
#define MULTIBOOT_HEADER_TAG_ADDRESS			2
#define MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS		3
#define MULTIBOOT_HEADER_TAG_CONSOLE_FLAGS		4
#define MULTIBOOT_HEADER_TAG_FRAMEBUFFER		5
#define MULTIBOOT_HEADER_TAG_MODULE_ALIGN		6
#define MULTIBOOT_HEADER_TAG_EFI_BS			7
#define MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS_EFI64	9
#define MULTIBOOT_HEADER_TAG_RELOCATABLE		10

#define MULTIBOOT_ARCHITECTURE_I386			0
#define MULTIBOOT_HEADER_TAG_OPTIONAL			1

#define MULTIBOOT_LOAD_PREFERENCE_NONE			0
#define MULTIBOOT_LOAD_PREFERENCE_LOW			1
#define MULTIBOOT_LOAD_PREFERENCE_HIGH			2

#define MULTIBOOT_CONSOLE_FLAGS_CONSOLE_REQUIRED	1
#define MULTIBOOT_CONSOLE_FLAGS_EGA_TEXT_SUPPORTED	2

#define MULTIBOOT_TAG_ALIGN				8
#define MULTIBOOT_TAG_TYPE_END				0
#define MULTIBOOT_TAG_TYPE_CMDLINE			1
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME		2
#define MULTIBOOT_TAG_TYPE_MODULE			3
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO		4
#define MULTIBOOT_TAG_TYPE_BOOTDEV			5
#define MULTIBOOT_TAG_TYPE_MMAP				6
#define MULTIBOOT_TAG_TYPE_VBE				7
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER			8
#define MULTIBOOT_TAG_TYPE_ELF_SECTIONS			9
#define MULTIBOOT_TAG_TYPE_APM				10
#define MULTIBOOT_TAG_TYPE_EFI32			11
#define MULTIBOOT_TAG_TYPE_EFI64			12
#define MULTIBOOT_TAG_TYPE_SMBIOS			13
#define MULTIBOOT_TAG_TYPE_ACPI_OLD			14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW			15
#define MULTIBOOT_TAG_TYPE_NETWORK			16
#define MULTIBOOT_TAG_TYPE_EFI_MMAP			17
#define MULTIBOOT_TAG_TYPE_EFI_BS			18
#define MULTIBOOT_TAG_TYPE_EFI32_IH			19
#define MULTIBOOT_TAG_TYPE_EFI64_IH			20
#define MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR		21

#ifndef __ASSEMBLER__

struct multiboot_header_tag {
	uint16_t type;
	uint16_t flags;
	uint32_t size;
};

struct multiboot2_mmap_entry {
	uint64_t addr;
	uint64_t len;
	uint32_t type;
	uint32_t zero;
};

struct multiboot_tag {
	uint32_t type;
	uint32_t size;
};

struct multiboot_tag_string {
	uint32_t type;
	uint32_t size;
	char string[];
};

struct multiboot_tag_mmap {
	uint32_t type;
	uint32_t size;
	uint32_t entry_size;
	uint32_t entry_version;
	struct multiboot2_mmap_entry entries[];
};

struct multiboot_tag_old_acpi {
	uint32_t type;
	uint32_t size;
	uint8_t rsdp[];
};

struct multiboot_tag_new_acpi {
	uint32_t type;
	uint32_t size;
	uint8_t rsdp[];
};

struct multiboot_tag_efi_mmap {
	uint32_t type;
	uint32_t size;
	uint32_t descr_size;
	uint32_t descr_vers;
	uint8_t efi_mmap[0];
};

struct multiboot_tag_load_base_addr {
	uint32_t type;
	uint32_t size;
	uint32_t load_base_addr;
};

extern void *multiboot2_addr;

#endif	/* !__ASSEMBLER__ */
