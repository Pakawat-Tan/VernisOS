# VernisOS — VernisFS ระบบไฟล์

> ไฟล์ที่เกี่ยวข้อง: `kernel/fs/vernisfs.c`, `include/vernisfs.h`

---

## ภาพรวม

VernisFS เป็น filesystem แบบ **flat** ที่เรียบง่าย ออกแบบมาให้ทำงานกับ ATA PIO disk โดยตรง
ไม่มี directory tree จริงๆ — ใช้ path string เป็น key แทน (`/etc/shadow`, `/tmp/log.txt`)

- ไฟล์สูงสุด: **32 ไฟล์**
- ขนาดไฟล์สูงสุด (ต่อ append): **4 KB** (ขนาดจริงไม่จำกัด แต่ append buffer จำกัดไว้)
- disk access: ATA PIO (polling, ไม่ใช้ DMA)
- ตำแหน่งบน disk: เริ่มต้นที่ **Sector 5120** (ออฟเซ็ต 2,621,440 bytes)

---

## Disk Layout

```
os.img Sector 5120 = VFS_START_SECTOR
┌──────────────────────────────────────────┐
│  Sector 5120      Superblock (512 bytes) │
├──────────────────────────────────────────┤
│  Sector 5121      File Table             │  ← entry 0–7   (64B × 8 = 512B)
│  Sector 5122      File Table             │  ← entry 8–15
│  Sector 5123      File Table             │  ← entry 16–23
│  Sector 5124      File Table             │  ← entry 24–31
├──────────────────────────────────────────┤
│  Sector 5125      Data Block 0           │  ← ไฟล์แรก
│  Sector 5126      Data Block 1           │
│  ...              ...                    │
│  Sector 5125+N    Data Block N           │
└──────────────────────────────────────────┘
```

`VFS_DATA_SECTOR = VFS_START_SECTOR + 1 + 4 = 5125`

---

## โครงสร้างข้อมูล

### Superblock (512 bytes)

```c
typedef struct {
    uint32_t magic;             // 0x53465600 ("VFS\0") — ตรวจสอบ filesystem ถูกต้อง
    uint16_t version;           // Version ปัจจุบัน = 1
    uint16_t file_count;        // จำนวนไฟล์ที่มีอยู่
    uint32_t total_data_sectors;// จำนวน data sector ทั้งหมด
    uint32_t first_free_sector; // sector ว่างถัดไป (relative to VFS_DATA_SECTOR)
    uint8_t  reserved[16];
} VfsSuperblock;
```

### FileEntry (64 bytes/entry, 32 entries รวม)

```c
typedef struct {
    char     filename[32];      // Full path เช่น "/etc/shadow", "/tmp/log.txt"
    uint32_t start_sector;      // ตำแหน่ง data (relative to VFS_DATA_SECTOR)
    uint32_t size;              // ขนาดไฟล์ (bytes)
    uint8_t  type;              // VFS_TYPE_EMPTY=0, VFS_TYPE_FILE=1, VFS_TYPE_DIRECTORY=2
    uint8_t  flags;             // VFS_FLAG_READONLY=0x01, VFS_FLAG_SYSTEM=0x02
    uint8_t  reserved[22];
} VfsFileEntry;
```

### Constants

```c
#define VFS_MAX_FILES      32
#define VFS_MAX_FILENAME   32
#define VFS_START_SECTOR   5120
#define VFS_MAGIC          0x53465600
#define VFS_TYPE_EMPTY     0
#define VFS_TYPE_FILE      1
#define VFS_TYPE_DIRECTORY 2
```

---

## API

### Initialize

```c
bool vfs_init(void);
```

อ่าน Superblock จาก Sector 5120 และตรวจสอบ magic number
- คืนค่า `true` ถ้า filesystem ถูกต้อง
- คืนค่า `false` ถ้าไม่พบ VernisFS (magic ผิด)

### ค้นหาไฟล์

```c
const VfsFileEntry *vfs_find_file(const char *path);
```

ค้นหา entry ด้วย path string ตรงๆ คืนค่า `NULL` ถ้าไม่พบ

### อ่านไฟล์

```c
int vfs_read_file(const char *path, uint8_t *buf, size_t max_len);
```

- อ่านข้อมูลไฟล์ไปใส่ `buf`
- คืนค่าจำนวน bytes ที่อ่านได้ หรือ `-1` ถ้าเกิดข้อผิดพลาด

### เขียนไฟล์ (สร้างใหม่หรือทับเดิม)

```c
int vfs_write_file(const char *path, const uint8_t *data, size_t len);
```

- ถ้าไฟล์มีอยู่แล้ว: ใช้ slot เดิมแต่จัดสรร data sector ใหม่ (sector เก่าไม่ถูกคืน)
- ถ้าไฟล์ยังไม่มี: สร้าง entry ใหม่
- จัดสรร sector จาก `first_free_sector` (monotonic — ไม่มี free-block reuse)
- flush metadata ทันที

### เพิ่มข้อมูลต่อท้ายไฟล์

```c
int vfs_append_file(const char *path, const uint8_t *data, size_t len);
```

- อ่านเนื้อหาเดิมเข้า static buffer 4 KB
- ต่อ `data` ต่อท้าย
- เขียนกลับทั้งหมดด้วย `vfs_write_file`
- **จำกัด**: ขนาดรวมสูงสุด 4 KB

### สร้างไดเรกทอรี

```c
int vfs_mkdir(const char *path);
```

สร้าง entry ชนิด `VFS_TYPE_DIRECTORY` ไม่มี data sectors

### ลบไฟล์

```c
int vfs_delete_file(const char *path);
```

- Set type เป็น `VFS_TYPE_EMPTY`
- ลด `file_count`
- flush metadata
- **หมายเหตุ**: data sectors ที่ใช้ไปจะไม่ถูกคืน (ไม่มี free list)

### List directory

```c
int vfs_list_dir(const char *dir_path, char out[][VFS_MAX_FILENAME], int max);
```

คืนรายชื่อไฟล์/directories ที่อยู่ใน `dir_path` โดยตรง (direct children เท่านั้น)

### ข้อมูลสถิติ

```c
uint16_t vfs_file_count(void);
```

---

## ATA PIO Implementation

VernisFS ใช้ ATA PIO (Programmed I/O) เข้าถึง disk โดยตรง:

```c
int vfs_ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf);
int vfs_ata_write_sectors(uint32_t lba, uint8_t count, uint8_t *buf);
```

### ขั้นตอนการอ่าน 1 sector

```
1. รอ BSY = 0  (port 0x1F7)
2. ส่ง drive select: outb(0x1F6, 0xE0 | ((lba>>24) & 0x0F))
3. ส่ง sector count: outb(0x1F2, count)
4. ส่ง LBA: outb(0x1F3, lba & 0xFF)
           outb(0x1F4, (lba>>8) & 0xFF)
           outb(0x1F5, (lba>>16) & 0xFF)
5. ส่ง command READ: outb(0x1F7, 0x20)
6. รอ BSY=0, DRQ=1  (polling loop)
7. อ่าน 256 words จาก port 0x1F0 (insw หรือ loop inw)
```

### Ports ที่ใช้

| Port | ทิศทาง | คำอธิบาย |
|------|--------|----------|
| `0x1F0` | R/W | Data (16-bit) |
| `0x1F2` | W | Sector Count |
| `0x1F3` | W | LBA bits 0–7 |
| `0x1F4` | W | LBA bits 8–15 |
| `0x1F5` | W | LBA bits 16–23 |
| `0x1F6` | W | Drive/Head (LBA mode) |
| `0x1F7` | W | Command register |
| `0x1F7` | R | Status register |

---

## ข้อจำกัดและ TODO

| ข้อจำกัด | รายละเอียด |
|---------|-----------|
| ไม่มี free block list | Deleted file sectors ไม่ถูกคืน — disk เต็มในที่สุด |
| append ขนาดจำกัด | Static buffer 4 KB ทำให้ไฟล์ใหญ่ append ไม่ได้ |
| ไม่มี directory tree จริง | path เป็นแค่ string key ไม่มี inode |
| ไม่มี locking | ไม่ thread-safe (แต่ VernisOS ยังไม่มี concurrent process) |
| ATA PIO ช้า | DMA ยังไม่ implement |

---

## ตัวอย่างการใช้งาน (CLI)

```
VernisOS> mkdir /etc
VernisOS> write /etc/hello.txt Hello, World!
VernisOS> cat /etc/hello.txt
Hello, World!
VernisOS> append /etc/hello.txt  This is appended.
VernisOS> ls /etc
  [file] hello.txt             29 B
VernisOS> rm /etc/hello.txt
VernisOS> ls /etc
  (empty)
```
