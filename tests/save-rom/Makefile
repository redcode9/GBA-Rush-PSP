# The save type test ROM, one target per save chip, each with its own save
# library ID string except save-notag, plus save-eeprom8k_wfirst, the
# 8 KiB EEPROM written before anything reads it (source/main.c). Builds
# $(OUT)/save-<chip>.gba with the rules of ../gba-rom.mk.

TARGETS := save-sram32 save-sram64 save-flash64 save-flash128 \
 save-eeprom512 save-eeprom8k save-eeprom8k_wfirst save-notag
GAME_CODE := GRSV
SOURCES := source/main.c source/cases_bus.c

FLAGS_save-sram32 := -DBACKUP_KIND=BACKUP_SRAM -DBACKUP_SIZE=0x8000
FLAGS_save-sram64 := -DBACKUP_KIND=BACKUP_SRAM -DBACKUP_SIZE=0x10000
FLAGS_save-flash64 := -DBACKUP_KIND=BACKUP_FLASH -DBACKUP_SIZE=0x10000
FLAGS_save-flash128 := -DBACKUP_KIND=BACKUP_FLASH -DBACKUP_SIZE=0x20000
FLAGS_save-eeprom512 := -DBACKUP_KIND=BACKUP_EEPROM -DBACKUP_SIZE=0x200
FLAGS_save-eeprom8k := -DBACKUP_KIND=BACKUP_EEPROM -DBACKUP_SIZE=0x2000
FLAGS_save-eeprom8k_wfirst := -DBACKUP_KIND=BACKUP_EEPROM \
 -DBACKUP_SIZE=0x2000 -DBACKUP_WRITE_FIRST
FLAGS_save-notag := -DBACKUP_KIND=BACKUP_SRAM -DBACKUP_SIZE=0x8000 \
 -DBACKUP_UNTAGGED

include ../gba-rom.mk
