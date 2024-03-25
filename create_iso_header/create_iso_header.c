#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>

#include <sys/types.h>

#include <endian.h>
#include <byteswap.h>

#if defined(__LITTLE_ENDIAN)
#define cpu_to_be32(val) bswap_32(val)
#define be32_to_cpu(val) bswap_32(val)
#define cpu_to_be16(val) bswap_16(val)
#define be16_to_cpu(val) bswap_16(val)
#else
#define cpu_to_be32(val) (val)
#define be32_to_cpu(val) (val)
#define cpu_to_be16(val) (val)
#define be16_to_cpu(val) (val)
#endif

#include "../include/gcm.h"

// Fixed structure:
// 0x00000000 + 0x0440	Disk header ("boot.bin")
// 0x00000440 + 0x2000	Disk header Information ("bi2.bin")
// 0x00002440 + 0x4000  Apploader image ("appldr.bin")
// 0x00006440 + 0x0040  FST ("fst.bin")
// 0x00006480 + 0x1960  BNR ("opening.bnr")
// 0x00007de0 + 0x0220  PADDING
// 0x00008000 + 0x????  DOL ("main.dol")

struct gcm_minimal_fst
{
    struct gcm_file_entry root;
    struct gcm_file_entry opening;
    char string_table[40];
} __attribute__((__packed__));


struct dhi_wrapper {
    struct gcm_disk_header_info dhi;
    uint8_t padding[0x2000 - sizeof(struct gcm_disk_header_info)];
} __attribute__((__packed__));

struct gcm_minimal_header
{
    struct gcm_disk_header dh;
    struct dhi_wrapper dhi_w;
    struct gcm_apploader_header al_header;
    uint8_t al_bin[0x4000 - sizeof(struct gcm_apploader_header)];
    struct gcm_minimal_fst fst_bin;
    uint8_t bnr_bin[0x1960];
    uint8_t padding[0x0220];
} __attribute__((__packed__));

void write_struct(char *path, void *obj, size_t size)
{
    // open file for writing
    FILE *outfile = fopen(path, "wb");
    if (outfile == NULL)
    {
        fprintf(stderr, "\nError opened file\n");
        exit(1);
    }

    // write struct to file
    int flag = 0;
    flag = fwrite(obj, size, 1, outfile);
    if (flag)
    {
        printf("successfully wrote %s\n", path);
    }
    else
        printf("Error Writing to File!\n");

    // close file
    fclose(outfile);
}

static void default_disk_header(struct gcm_disk_header *dh)
{
    memset(dh, 0, sizeof(*dh));

    memcpy(dh->info.game_code, "GBLA", 4); /* Gamecube BootLoader */
    memcpy(dh->info.maker_code, "OB", 2);  /* OffBroadway */
    dh->info.magic = cpu_to_be32(0xc2339f3d);

    dh->info.audio_streaming = 1; // enable audio streaming
    dh->info.stream_buffer_size - 0;

    strcpy(dh->game_name, "GAMECUBE COMPAT BOOTLOADER");

    dh->layout.dol_offset = cpu_to_be32(0x00008000);
    dh->layout.fst_offset = cpu_to_be32(0x00006440);
    dh->layout.fst_size = cpu_to_be32(0x00000040);
    dh->layout.fst_max_size = dh->layout.fst_size;
    dh->layout.user_offset = cpu_to_be32(0);             // skip
    dh->layout.user_size = cpu_to_be32(4 * 1024 * 1024); /* 4MB */
    dh->layout.disk_size = cpu_to_be32(0x56fe8000);      /* 1.4GB */
}

// TODO: make country configurable for Stock booting (without IPL patching)
static void default_disk_header_info(struct gcm_disk_header_info *dhi)
{
    memset(dhi, 0, sizeof(*dhi));

    dhi->simulated_memory_size = cpu_to_be32(0x01800000);
    dhi->country_code = cpu_to_be32(1); /* 0=jap 1=usa 2=eur 3=ODE */
    dhi->unknown_1 = cpu_to_be32(1);
}

static void default_apploader_header(struct gcm_apploader_header *ah)
{
    memset(ah, 0, sizeof(*ah));

    memcpy(ah->date, "2023/07/23", 10);
    ah->entry_point = cpu_to_be32(0x81200000);
}

static void default_minimal_fst(struct gcm_minimal_fst *fst_bin)
{
    // root
    fst_bin->root.root_dir.zeroed_1 = cpu_to_be32(0x01000000);
    fst_bin->root.root_dir.zeroed_2 = cpu_to_be32(0x0);
    fst_bin->root.root_dir.num_entries = cpu_to_be32(2);

    // banner
    fst_bin->opening.file.fname_offset = cpu_to_be32(0);
    fst_bin->opening.file.file_length = cpu_to_be32(0x1960);
    fst_bin->opening.file.file_offset = cpu_to_be32(0x00006480);

    // string table
    strcpy(fst_bin->string_table, "openning.bnr");
}

uint8_t *read_file_into_buffer(const char *filename, size_t *size)
{
    FILE *file;
    uint8_t *buffer;

    // Open the file
    file = fopen(filename, "rb");
    if (!file)
    {
        perror("Failed to open file");
        return NULL;
    }

    // Seek to the end of the file
    if (fseek(file, 0, SEEK_END) != 0)
    {
        perror("Failed to seek to end of file");
        fclose(file);
        return NULL;
    }

    // Get the current file position, which is the file's size
    long filesize = ftell(file);
    if (filesize == -1)
    {
        perror("Failed to determine file size");
        fclose(file);
        return NULL;
    }

    // Go back to the start of the file
    rewind(file);

    // Allocate memory for the entire file
    buffer = (char *)malloc(filesize);
    if (!buffer)
    {
        perror("Failed to allocate memory for file");
        fclose(file);
        return NULL;
    }

    // Read the entire file into the buffer
    size_t readSize = fread(buffer, 1, filesize, file);
    if (readSize != filesize)
    {
        perror("Failed to read file into buffer");
        free(buffer);
        fclose(file);
        return NULL;
    }

    // Close the file
    fclose(file);

    // Set the size (if the caller is interested)
    if (size != NULL)
    {
        *size = filesize;
    }

    return buffer;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("usage: create_iso_header <size of apploader>\n");
        exit(1);
    }

    struct gcm_minimal_header header;
    default_disk_header(&header.dh);
    default_disk_header_info(&header.dhi_w.dhi);
    default_apploader_header(&header.al_header);
    default_minimal_fst(&header.fst_bin);

    char *apploader_bin_path = argv[1];
    size_t apploader_bin_size = 0;
    uint8_t *apploader_bin = read_file_into_buffer(apploader_bin_path, &apploader_bin_size);
    header.al_header.size = cpu_to_be32(apploader_bin_size);
    memcpy(&header.al_bin[0], apploader_bin, apploader_bin_size);

    char *banner_bin_path = argv[2];
    size_t banner_bin_size = 0;
    uint8_t *banner_bin = read_file_into_buffer(banner_bin_path, &banner_bin_size);
    memcpy(&header.bnr_bin[0], banner_bin, banner_bin_size);

    // uint32_t header_offset = (uint32_t)&header;
    // printf("boot offset = %x (%x) sized %s\n", (uint32_t)&header.dh - header_offset, (uint32_t)&header.dh - header_offset == 0x00000000, sizeof(struct gcm_disk_header) == 0x0440 ? "GOOD" : "BAD");
    // printf("bi2 offset = %x (%x) sized %s\n", (uint32_t)&header.dhi_w - header_offset, (uint32_t)&header.dhi_w - header_offset == 0x00000440, sizeof(struct dhi_wrapper) == 0x2000 ? "GOOD" : "BAD");
    // printf("al_header offset = %x (%x) sized %s\n", (uint32_t)&header.al_header - header_offset, (uint32_t)&header.al_header - header_offset == 0x00002440, "GOOD");
    // printf("fst offset = %x (%x) sized %s\n", (uint32_t)&header.fst_bin - header_offset, (uint32_t)&header.fst_bin - header_offset == 0x00006440, sizeof(struct gcm_minimal_fst) == 0x0040 ? "GOOD" : "BAD");
    // printf("bnr offset = %x (%x) total %x\n", (uint32_t)&header.bnr_bin - header_offset, (uint32_t)&header.bnr_bin - header_offset == 0x00006480, sizeof(struct gcm_minimal_header));

    // write_struct("boot.bin", &header.dh, sizeof(struct gcm_disk_header));
    // write_struct("bi2.bin", &header.dhi_w.dhi, sizeof(struct gcm_disk_header_info));
    // write_struct("al_header.bin", &header.al_header, sizeof(struct gcm_apploader_header));
    // write_struct("fst.bin", &header.fst_bin, sizeof(struct gcm_minimal_fst));
    write_struct("disc_header.iso", &header, sizeof(struct gcm_minimal_header));

    return 0;
}
