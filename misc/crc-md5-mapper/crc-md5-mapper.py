# this script creates a list of CRC32 hashes of the first 512 bytes from the 
# first and last PRG-ROM rom and the MD5 hash of the ROM file, skipping the first 16 bytes (iNES header)
#
# usage example: python crc-md5-mapper.py > games.txt
#
# the games.txt is the file located on /data folder of the nes-esp-firmware mapping CRCs and MD5 hashes (RA hashes) of the ROMs
#
# this script used the goodnes romset to create the games.txt file of the nes-esp-firmware

import hashlib
import binascii
import os
import re

# calculates the MD5 hash of a file skipping the first 16 bytes (iNES header)
def calculate_md5(file_path):
    hash_md5 = hashlib.md5()
    with open(file_path, "rb") as f:
        # Skip the first 16 bytes (iNES header)
        f.seek(16)
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

# calculates the CRC32 hash of the first 512 bytes of a file (first rom bank) skipping the first 16 bytes (iNES header)
def calculate_crc32_first_512_bytes(file_path):
    with open(file_path, 'rb') as f:
        # Skip the first 16 bytes (iNES header)
        f.seek(16)
        first_512_bytes = f.read(512)
    crc32 = binascii.crc32(first_512_bytes)
    return format(crc32 & 0xFFFFFFFF, '08x')

# reads iNES header and returns the PRG ROM size in bytes
def ler_ines_header(caminho_arquivo):
    with open(caminho_arquivo, "rb") as f:
        header = f.read(16)  # read the iNES header (16 bytes)

        if header[:4] != b"NES\x1A":
            print(caminho_arquivo)
            raise ValueError("File is not a valid iNES ROM")

        prg_rom_size = header[4] * 16 * 1024  # PRG ROM in bytes (each unit = 16 KB)
        return prg_rom_size

# calculates the CRC32 hash of the last 512 bytes of the PRG-ROM (last rom bank) skipping the first 16 bytes (iNES header)
def calculate_crc32_first_last_512_bytes(file_path):
    prg_rom_size = ler_ines_header(file_path)
    if prg_rom_size < 8192:
        return "FFFFFFFF"
    with open(file_path, 'rb') as f:
        # Skip the first 16 bytes (iNES header)
        fileSize = os.path.getsize(file_path)
        f.seek(16 + prg_rom_size - 8192)
        first_512_bytes = f.read(512)
    crc32 = binascii.crc32(first_512_bytes)
    return format(crc32 & 0xFFFFFFFF, '08x')


# list all files in a directory
def list_files(dir):
    caminhos = []
    for raiz, dirs, arquivos in os.walk(dir):
        for arquivo in arquivos:
            caminho_completo = os.path.join(raiz, arquivo)
            caminhos.append(caminho_completo)
    return caminhos



# main
root_games_dir = './goodnes' # path to the goodnes romset (or any other romset)
game_list = list_files(root_games_dir)

# sort the list of games by region in this order (Japan-USA, USA-Europe, USA, World, rest) - this is valid for goodnes dataset
game_list.sort(key=lambda x: 0 if "/World/" in x.replace("\\", "/") else 1)
game_list.sort(key=lambda x: 0 if "/USA/" in x.replace("\\", "/") else 1)
game_list.sort(key=lambda x: 0 if "/USA-Europe/" in x.replace("\\", "/") else 1)
game_list.sort(key=lambda x: 0 if "/Japan-USA/" in x.replace("\\", "/") else 1)

# sort "Nintendo World Championships 1990 (U) [!].nes" to the botton of the list
# this is a special case, since the second CRC matches with RAD RACER (because NWC 1990 has a version of Rad Racer inside)
# messing with RAD RACER identification (since its first CRC just match in the first try because of the mapper used)
game_list.sort(key=lambda x: 1 if "Nintendo World Championships 1990 (U) [!].nes" in x.replace("\\", "/") else 0)

game_info_list = []
crc_collisions = []
for game in game_list:
    md5_hash = calculate_md5(game)    
    crc32_first_512_hash = calculate_crc32_first_512_bytes(game)
    crc32_first_last_512_hash = calculate_crc32_first_last_512_bytes(game)    
    crc32_first_512_hash = crc32_first_512_hash.upper()
    crc32_first_last_512_hash = crc32_first_last_512_hash.upper()
    
    game_crc_md5_info = crc32_first_512_hash + "," + crc32_first_last_512_hash + "=" + md5_hash
    if(game_crc_md5_info not in game_info_list):
        crc_begin_end = crc32_first_512_hash + "," + crc32_first_last_512_hash        
        game_info_list.append(game_crc_md5_info)
        game_name = str(game)
        
        # try to keep just the original version of the game (good dump)
        pattern = r".*\((?:U|E|J|W|JU|UE)\)(?: \(V\d+\.\d+\))? \[!\]\.nes$"
        match = re.match(pattern, game_name)
        if (match):            
            # if there is a CRC collision, it will keep the first one (portencially the version used by RA) - 172 collisions on goodnes dataset
            if (crc_begin_end not in crc_collisions):
                print(game_crc_md5_info)
                crc_collisions.append(crc_begin_end)
        
        


