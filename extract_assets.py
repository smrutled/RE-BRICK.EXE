import os
import struct

# The binary path
EXE_PATH = r"E:\PROGRAMS\WINGAMES\ARCADE\BRICK\BRICK.EXE"
OUT_DIR = r"c:\Projects\Reverse"

def extract_from_exe():
    if not os.path.exists(EXE_PATH):
        print(f"Exe not found at {EXE_PATH}")
        return False

    with open(EXE_PATH, "rb") as f:
        data = f.read()

    # Verify MZ header
    if data[:2] != b"MZ":
        print("Not an MZ executable")
        return False

    ne_offset = struct.unpack_from("<H", data, 0x3C)[0]
    if data[ne_offset:ne_offset+2] != b"NE":
        print("Not an NE executable")
        return False

    # NE Header offsets
    # 0x24: Resource Table offset relative to NE header
    rsrc_table_offset = ne_offset + struct.unpack_from("<H", data, ne_offset + 0x24)[0]
    align_shift = struct.unpack_from("<H", data, rsrc_table_offset)[0]

    pos = rsrc_table_offset + 2
    resources = {}
    while True:
        type_id = struct.unpack_from("<H", data, pos)[0]
        if type_id == 0:
            break
        num_resources = struct.unpack_from("<H", data, pos + 2)[0]
        pos += 8 # type_id (2), count (2), reserved (4)

        for _ in range(num_resources):
            offset = struct.unpack_from("<H", data, pos)[0] << align_shift
            length = struct.unpack_from("<H", data, pos + 2)[0] << align_shift
            flags = struct.unpack_from("<H", data, pos + 4)[0]
            res_id = struct.unpack_from("<H", data, pos + 6)[0]
            pos += 12

            # Clean res_id (strip integer flag 0x8000)
            id_val = res_id & 0x7FFF if (res_id & 0x8000) else res_id
            type_val = type_id & 0x7FFF if (type_id & 0x8000) else type_id

            res_data = data[offset:offset+length]
            resources[(type_val, id_val)] = res_data
            print(f"Found resource Type={type_val}, ID={id_val}, Size={length} bytes at offset {hex(offset)}")

    # Type 2 is RT_BITMAP, Type 3 is RT_ICON, Type 14 is RT_GROUP_ICON
    # Let's save bitmaps with standard BMP file headers:
    # BITMAPFILEHEADER: "BM" (2), bfSize (4), bfReserved1 (2), bfReserved2 (2), bfOffBits (4)
    for (t, i), raw in resources.items():
        if t == 2: # RT_BITMAP
            # DIB in resource starts with BITMAPINFOHEADER (40 bytes)
            biSize, biWidth, biHeight, biPlanes, biBitCount = struct.unpack_from("<IIIHH", raw, 0)
            num_colors = 1 << biBitCount if biBitCount <= 8 else 0
            palette_size = num_colors * 4
            off_bits = 14 + biSize + palette_size
            file_size = 14 + len(raw)

            file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, off_bits)
            bmp_filename = os.path.join(OUT_DIR, f"bitmap_{i}.bmp")
            with open(bmp_filename, "wb") as bf:
                bf.write(file_header + raw)
            print(f"Saved bitmap_{i}.bmp (Width: {biWidth}, Height: {biHeight}, BPP: {biBitCount})")

        elif t == 3: # RT_ICON
            # Save raw icon image
            pass

    # Build .ico file from RT_GROUP_ICON (14) and RT_ICON (3)
    # If Group Icon exists:
    for (t, i), grp in resources.items():
        if t == 14: # RT_GROUP_ICON
            # GRPICONDIR: idReserved (2), idType (2), idCount (2)
            idReserved, idType, idCount = struct.unpack_from("<HHH", grp, 0)
            ico_entries = []
            ico_images = []
            offset_in_ico = 6 + 16 * idCount

            for entry_idx in range(idCount):
                entry_pos = 6 + entry_idx * 14
                bWidth, bHeight, bColorCount, bReserved, wPlanes, wBitCount, dwBytesInRes, nID = struct.unpack_from("<BBBBHHII", grp, entry_pos)
                icon_data = resources.get((3, nID & 0x7FFF))
                if not icon_data:
                    # try without masking
                    icon_data = resources.get((3, nID))
                if icon_data:
                    # In .ico file, directory entry has dwImageOffset (4 bytes) instead of nID (2 bytes)
                    ico_entry = struct.pack("<BBBBHHII", bWidth, bHeight, bColorCount, bReserved, wPlanes, wBitCount, len(icon_data), offset_in_ico)
                    ico_entries.append(ico_entry)
                    ico_images.append(icon_data)
                    offset_in_ico += len(icon_data)

            if ico_entries:
                ico_path = os.path.join(OUT_DIR, "brick.ico")
                with open(ico_path, "wb") as icof:
                    icof.write(struct.pack("<HHH", 0, 1, len(ico_entries)))
                    for e in ico_entries:
                        icof.write(e)
                    for img in ico_images:
                        icof.write(img)
                print(f"Saved brick.ico with {len(ico_entries)} image(s)")

    return True

if __name__ == "__main__":
    extract_from_exe()
