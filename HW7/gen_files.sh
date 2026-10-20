#!/bin/bash
# Sinh du lieu kiem thu co dinh (cung noi dung tren moi may)
printf 'Hello! This is a sample text file for HW7 - IT4062.\nTCP Reliable File Transfer - Sliding Window.\nLine 3: 0123456789 ABCDEFGHIJ\n' > sample_text.txt
: > empty.bin
gen() { openssl enc -aes-128-ctr -K 000102030405060708090a0b0c0d0e0f \
        -iv 00000000000000000000000000000000 < /dev/zero 2>/dev/null | head -c "$1" > "$2"; }
gen 4096      one_chunk.bin
gen 4097      chunk_plus1.bin
gen 1048576   medium.bin
gen 104857600 large_data.bin
md5sum sample_text.txt empty.bin one_chunk.bin chunk_plus1.bin medium.bin large_data.bin
