#pragma once

enum {
	CBW_SIZE = 31,
	CBW_SIGNATURE = 0x43425355,
	CBW_IN = (1 << 7),
	CBW_OUT = (0 << 7)
};

enum {
	CSW_SIZE = 13,
	CSW_SIGNATURE = 0x53425355,
};

enum {
	SCSI_TEST_UNIT_READY = 0x00,
	SCSI_REQUEST_SENSE = 0x03,
	SCSI_INQUIRY = 0x12,
	SCSI_MODE_SENSE = 0x1A,
	SCSI_START_STOP = 0x1B,
	SCSI_READ_FORMAT_CAPACITY = 0x23,
	SCSI_READ_CAPACITY = 0x25,
	SCSI_READ_10 = 0x28,
	SCSI_WRITE_10 = 0x2A
};

enum {
	SCSI_SENSE_REPLY_SIZE = 18,
	SCSI_SENSE_NOT_READY = 0x02,
	SCSI_SENSE_MEDIUM_ERROR = 0x03,
	SCSI_SENSE_HARDWARE_ERROR = 0x04,
	SCSI_SENSE_UNIT_ATTENTION = 0x06,
};

enum {
	USB_CLASS_MASS_STORAGE = 0x08,
	MASS_STORAGE_RBC_COMMANDS = 0x01,
	MASS_STORAGE_ATA_COMMANDS,
	MASS_STORAGE_QIC_COMMANDS,
	MASS_STORAGE_UFI_COMMANDS,
	MASS_STORAGE_SFF8070_COMMANDS,
	MASS_STORAGE_SCSI_COMMANDS,
	MASS_STORAGE_BULK_ONLY = 0x50
};

enum {
	USBSTORAGE_GET_MAX_LUN = 0xFE,
	USBSTORAGE_RESET = 0xFF,
};

enum {
	USB_ENDPOINT_BULK = 0x02,
};

enum {
	USBSTORAGE_CYCLE_RETRIES = 3,
	USBSTORAGE_TIMEOUT = 2
};

enum {
	MAX_TRANSFER_SIZE_V2 = 0x4000, // 4 pages
	MAX_TRANSFER_SIZE_V1 = 0x1000, // 1 page
};

enum {
	// 1MB buffer at start of DDR;
	// however due to IOS USBv5 bug the first 32 bytes cannot be used.
	// Thus, ignore the first page.
	// Anyway, MAX_TRANSFER_SIZE_V2 * USB_COUNT_DEVICES == 0x80000,
	// so even with the first page unused,
	// buffers for every possible device are guaranteed to fit.
	// Start half way through this buffer, let SDMC driver use the first half.
	USB_BUFFER_PHYS_START = 0x10080000, // 0x10001000,
	USB_BUFFER_LENGTH = 0x80000, // 0x100000 - 0x1000,
	USB_BUFFER_PHYS_END = USB_BUFFER_PHYS_START + USB_BUFFER_LENGTH
};

typedef struct ARC_LE _USBMS_CBW {
	ULONG Signature;
	ULONG Tag;
	ULONG Length;
	UCHAR Flags;
	UCHAR Lun;
	UCHAR CbLength;
	UCHAR Cb[16];
} USBMS_CBW, *PUSBMS_CBW;
_Static_assert(sizeof(USBMS_CBW) == 32);
_Static_assert(__builtin_offsetof(USBMS_CBW, Flags) == 12);
_Static_assert(__builtin_offsetof(USBMS_CBW, Lun) == 13);
_Static_assert(__builtin_offsetof(USBMS_CBW, CbLength) == 14);
_Static_assert(__builtin_offsetof(USBMS_CBW, Cb) == 15);

typedef struct ARC_LE _USBMS_CSW {
	ULONG Signature;
	ULONG Tag;
	ULONG DataResidue;
	UCHAR Status;
} USBMS_CSW, *PUSBMS_CSW;
_Static_assert(sizeof(USBMS_CSW) == 16);
_Static_assert(__builtin_offsetof(USBMS_CSW, Status) == 12);

typedef struct ARC_BE ARC_PACKED _SCSI_FORMAT_CAPACITY_ENTRY {
	ULONG NumberOfBlocks;
	UCHAR Flags;
	UCHAR BlockLengthHigh;
	USHORT BlockLengthLow;
} SCSI_FORMAT_CAPACITY_ENTRY, *PSCSI_FORMAT_CAPACITY_ENTRY;

typedef struct ARC_BE ARC_PACKED _SCSI_FORMAT_CAPACITY_LIST {
	UCHAR Reserved[3];
	UCHAR Length;
	SCSI_FORMAT_CAPACITY_ENTRY Entries[0];
} SCSI_FORMAT_CAPACITY_LIST, *PSCSI_FORMAT_CAPACITY_LIST;

typedef struct ARC_BE _SCSI_READ_CAPACITY_RES {
	ULONG SectorCount;
	ULONG SectorSize;
} SCSI_READ_CAPACITY_RES, *PSCSI_READ_CAPACITY_RES;