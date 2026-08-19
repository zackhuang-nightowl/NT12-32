#!/bin/sh
###############################################################################
# nvr_do_update.sh — 把已校验的 squashfs 烧到非活动 A/B rootfs，切槽后重启。
#
# 用法: nvr_do_update.sh <squashfs.img>
#
# 槽位: mtd6=rootfs(slot 0) / mtd7=rootfs1(slot 1)
# 当前槽读 /User/ROOTFS_MTD（0|1）；写对面槽，活动槽不动（救砖备份）。
###############################################################################

IMG="$1"
if [ -z "$IMG" ] || [ ! -f "$IMG" ]; then
    echo "nvr_do_update: missing image" >&2
    exit 1
fi

magic=$(dd if="$IMG" bs=1 count=4 2>/dev/null)
if [ "$magic" != "hsqs" ]; then
    echo "nvr_do_update: not squashfs (need hsqs), refuse nandwrite" >&2
    exit 1
fi

# 40MB rootfs 上限
sz=$(wc -c < "$IMG" 2>/dev/null | tr -d ' ')
if [ -n "$sz" ] && [ "$sz" -gt 41943040 ]; then
    echo "nvr_do_update: image too large ($sz > 40MB)" >&2
    exit 1
fi

SLOT_FILE="/User/ROOTFS_MTD"
if [ ! -e "$SLOT_FILE" ]; then
    if [ -d /User ]; then
        SLOT_FILE="/User/ROOTFS_MTD"
    elif [ -d /flash ]; then
        SLOT_FILE="/flash/ROOTFS_MTD"
    fi
fi

cur=$(cat "$SLOT_FILE" 2>/dev/null | tr -dc '01')
[ -z "$cur" ] && cur=0

if [ "$cur" = "0" ]; then
    next=1
    mtd=/dev/mtd7
else
    next=0
    mtd=/dev/mtd6
fi

if [ ! -e "$mtd" ]; then
    echo "nvr_do_update: $mtd missing" >&2
    exit 1
fi

echo "nvr_do_update: current slot=$cur, flash $mtd as slot $next (keep $cur as backup)"

flash_erase "$mtd" 0 0 || { echo "nvr_do_update: flash_erase failed" >&2; exit 1; }
nandwrite -p "$mtd" "$IMG" || { echo "nvr_do_update: nandwrite failed" >&2; exit 1; }

mkdir -p "$(dirname "$SLOT_FILE")" 2>/dev/null
echo "$next" > "$SLOT_FILE" || { echo "nvr_do_update: cannot write $SLOT_FILE" >&2; exit 1; }
sync
echo "nvr_do_update: switched to slot $next, reboot"
reboot
exit 0
