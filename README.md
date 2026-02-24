# snbdit
IO tool to write on a file (can be a disk) for a size with a pattern. Verify data, fail if mismatch pattern.

//# Write pattern to file
//./snb_dit /tmp/testfile.bin 4096 write 0xDEADBEEF

//# Read and verify
//./snb_dit /tmp/testfile.bin 4096 read 0xDEADBEEF

//# Write + Read + Verify in one shot
//./snb_dit /tmp/testfile.bin 4096 readwrite 0xDEADBEEF
