#!/bin/bash
set -e

# Kill everything
killall moveblocks weston MiniBrowser epiphany Xorg openbox 2>/dev/null || true
sleep 1

# Run moveblocks to init gfbg
export QT_QPA_PLATFORM_PLUGIN_PATH=/opt/plugins QT_QPA_PLATFORM=linuxfb
/opt/sample/qt/moveblocks &
MBPID=$!
sleep 4
kill $MBPID 2>/dev/null
wait $MBPID 2>/dev/null
sleep 1

echo "=== FB0 state after moveblocks ==="
cat /proc/umap/gfbg 2>/dev/null | head -5

echo "=== Test writing pixel to fb0 ==="
python3 -c "
import os, struct, mmap
fd = os.open('/dev/fb0', os.O_RDWR)
vinfo = bytearray(160)
import fcntl
fcntl.ioctl(fd, 0x4600, vinfo, True)
xres, yres = struct.unpack_from('II', vinfo, 0)
bpp = struct.unpack_from('I', vinfo, 24)[0]
print(f'Resolution: {xres}x{yres} {bpp}bpp')

finfo = bytearray(68)
fcntl.ioctl(fd, 0x4602, finfo, True)
slen = struct.unpack_from('I', finfo, 32)[0]
print(f'smem_len: {slen}')

# mmap and write
size = xres * yres * (bpp // 8)
m = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_WRITE)
# Fill first 100 pixels red
for y in range(100):
    for x in range(100):
        off = (y * xres + x) * 2
        m[off:off+2] = struct.pack('H', 0x7C00)  # red in RGB565
m.flush()
m.close()
os.close(fd)
print('Pixel write OK')
"

echo "DONE"