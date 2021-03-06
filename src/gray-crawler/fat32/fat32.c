/*****************************************************************************
 * fat32.c                                                                   *
 *                                                                           *
 * Analyze a FAT32 file system and produce BSON-serialized metadata.         *
 *                                                                           *
 *                                                                           *
 *   Authors: Sang Jin Han <shan1@andrew.cmu.edu>                            *
 *            Wolfgang Richter <wolf@cs.cmu.edu>                             *
 *                                                                           *
 *                                                                           *
 *   Copyright 2014-2015 Carnegie Mellon University                          *
 *                                                                           *
 *   Licensed under the Apache License, Version 2.0 (the "License");         *
 *   you may not use this file except in compliance with the License.        *
 *   You may obtain a copy of the License at                                 *
 *                                                                           *
 *       http://www.apache.org/licenses/LICENSE-2.0                          *
 *                                                                           *
 *   Unless required by applicable law or agreed to in writing, software     *
 *   distributed under the License is distributed on an "AS IS" BASIS,       *
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
 *   See the License for the specific language governing permissions and     *
 *   limitations under the License.                                          *
 *****************************************************************************/
#define _LARGEFILE64_SOURCE

#include <sys/types.h>

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bitarray.h"
#include "bson.h"
#include "color.h"
#include "fat32.h"
#include "util.h"

#define SECTOR_SIZE 512

int fat32_probe(int disk, struct fs* fs)
{
    struct fat32_volumeID* volumeID = malloc(sizeof(struct fat32_volumeID));
    char *volLab = calloc(1, 12);

    if (volumeID == NULL)
    {
        fprintf_light_red(stderr, "Error allocating space for "
                                  "'struct fat32_volumeID'.\n");
        return -1;
    }

    if (volLab == NULL)
    {
        fprintf_light_red(stderr, "Error allocating volLab.\n");
        return -1;
    }

    fs->fs_info = volumeID;

    if (fs->pt_off == 0)
    {
        fprintf_light_red(stderr, "fat32 probe failed on partition at offset: "
                                  "0x%.16"PRIx64".\n", fs->pt_off);
        return -1;
    }

    if (lseek64(disk, (off64_t) (fs->pt_off + 0x0B), SEEK_SET) == (off64_t) -1)
    {
        fprintf_light_red(stderr, "Error seeking while reading FAT32 "
                                  "VolID.\n");
        return -1;
    }

    if (read(disk, (void*)&volumeID->bytes_per_sector, sizeof(uint16_t)) != 
             sizeof(uint16_t))
    {
        fprintf_light_red(stderr, "Error while trying to read fat32 "
                                  "bytes_per_sector.\n");
        return -1;
    }

    if (lseek64(disk, (off64_t) (fs->pt_off + 0x0D), SEEK_SET) == (off64_t) -1)
    {
        fprintf_light_red(stderr, "Error seeking to FAT32 "
                                  "sectors_per_cluster.\n");
        return -1;
    }

    if (read(disk, (void*)&volumeID->sectors_per_cluster, sizeof(uint8_t)) != 
          sizeof(uint8_t))
    {
        fprintf_light_red(stderr, "Error while trying to read fat32 "
                                  "sectors_per_cluster.\n");
        return -1;
    }

    if (lseek64(disk, (off64_t) (fs->pt_off + 0x0E), SEEK_SET) == (off64_t) -1)
    {
        fprintf_light_red(stderr, "Error while seeking to FAT32 "
                                  "num_reserved_sectors.\n");
        return -1;
    }

    if (read(disk, (void*)&volumeID->num_reserved_sectors, sizeof(uint16_t)) !=
        sizeof(uint16_t))
    {
        fprintf_light_red(stderr, "Error while trying to read fat32 "
                                  "num_reserved_sectors.\n");
        return -1;
    }

    if (lseek64(disk, (off64_t) (fs->pt_off + 0x10), SEEK_SET) == (off64_t) -1)
    {
        fprintf_light_red(stderr, "Error seeking to FAT32 num_fats.\n");
        return -1;
    }

    if (read(disk, (void*)&volumeID->num_fats, sizeof(uint8_t)) != 
        sizeof(uint8_t))
    {
        fprintf_light_red(stderr, "Error while trying to read fat32 "
                                  "num_fats.\n");
        return -1;
    }

    if (lseek64(disk, (off64_t) (fs->pt_off + 0x24), SEEK_SET) == (off64_t) -1)
    {
        fprintf_light_red(stderr, "Error while trying to seek to FAT32 "
                                  "sectors_per_fat.\n");
        return -1;
    }

    if (read(disk, (void*)&volumeID->sectors_per_fat, sizeof(uint32_t)) != 
          sizeof(uint32_t))
    {
        fprintf_light_red(stderr, "Error while trying to read fat32 "
                                  "sectors_per_fat.\n");
        return -1;
    }

    if (lseek64(disk, (off64_t) (fs->pt_off + 0x2C), SEEK_SET) == (off64_t) -1)
    {
        fprintf_light_red(stderr, "Error while seeking to FAT32 "
                                  "root_dir_first_cluster.\n");
        return -1;
    }

    if (read(disk, (void*)&volumeID->root_dir_first_cluster,
             sizeof(uint32_t)) != sizeof(uint32_t))
    {
        fprintf_light_red(stderr, "Error while trying to read fat32 "
                                  "root_dir_first_cluster.\n");
        return -1;
    }

    if (lseek64(disk, (off64_t) (fs->pt_off + 71), SEEK_SET) == (off64_t) -1)
    {
        fprintf_light_red(stderr, "Error while seeking to FAT32 volLab.\n");
        return -1;
    }

    if (read(disk, (void*) volLab, 11) != 11)
    {
        fprintf_light_red(stderr, "Error while trying to read fat32 "
                                  "signature.\n");
        return -1;
    }

    free(volLab); /* TODO: actually use this */

    if (lseek64(disk, (off64_t) (fs->pt_off + 0x1FE), SEEK_SET) ==
        (off64_t) -1)
    {
        fprintf_light_red(stderr, "Error while seeking to FAT32 signature.\n");
        return -1;
    }

    if (read(disk, (void*)&volumeID->signature, sizeof(uint32_t)) != 
          sizeof(uint32_t))
    {
        fprintf_light_red(stderr, "Error while trying to read fat32 "
                                  "signature.\n");
        return -1;
    }

    if (volumeID->signature != 0xAA55)
    {
        fprintf_light_red(stderr, "Fat32 signature does not match 0xAA55.\n");
        return -1;
    }

    return 0;
}

int64_t get_cluster_addr(struct fs* fs, uint32_t cluster_number) {  
  struct fat32_volumeID* volID = fs->fs_info;
  int64_t cluster_begin_lba = (int64_t)volID->num_reserved_sectors +
                              (volID->num_fats * volID->sectors_per_fat);
  printf("cluster_begin_lba %" PRId64 "\n", cluster_begin_lba);
  printf("fs PTOFF %" PRId64 "\n", fs->pt_off);
  /* TODO: Define sector size. */
  return fs->pt_off + (int64_t)SECTOR_SIZE*(cluster_begin_lba +
         (cluster_number - 2) * volID->sectors_per_cluster);
}

uint32_t get_fat_entry(int disk, int cluster_num, struct fs* fs) {
    struct fat32_volumeID* volID = fs->fs_info;
    int64_t fat_begin = fs->pt_off + volID->num_reserved_sectors * SECTOR_SIZE;
    int64_t fat_entry_addr = fat_begin + (cluster_num * 4);
    off64_t curr_offset = lseek64(disk, 0, SEEK_CUR);
    uint32_t result;
    lseek64(disk, (off64_t) (fat_entry_addr), SEEK_SET);

    if (read(disk, (void*)&result, 4) != 4)
    {
        fprintf_light_red(stderr, "Error while trying to read fat entry.\n");
        return -1;
    }

    /* defensively set offset back */
    lseek64(disk, curr_offset, SEEK_SET);

    return result;
}

char* read_name_long_entry(unsigned char* entry)
{
    char *name = calloc(1, 14);
    int idx; int i;
    for (i = 0; i < 13; i++) 
    {
        if (i < 5)
        {
            idx = (2 * i) + 1;
        }
        else if (i < 11)
        {
            idx = 2 * (i - 5) + 14;
        }
        else
        {
            idx = 2 * (i - 11) + 28;
        }
        char fst_byte = *(entry + idx + 1);
        //char ninth_bit = *(entry + idx) >> 7;
        if (fst_byte) {
          *(name + i) = 0x3F;
        } else {
          memcpy(name + i, entry + idx, 1);
        }
        if (!*(entry + idx)) break;
    }
    return name;
}

char* read_long_entries(int disk, unsigned char* last_entry, int* offset)
{
  bool fst_long_entry = false;
  unsigned char* entry = last_entry;
  char* name = NULL;
  while (!fst_long_entry) 
  {
    if (!entry[0]) break;
    char* entry_name = read_name_long_entry(entry);
    if (!name) 
    {
      name = entry_name;
    }
    else 
    {
      char* temp = name;
      name = calloc(1, strlen(name) + strlen(entry_name) + 1);
      strcpy(name, entry_name); strcat(name, temp);
      free(temp);
    }
    if (!((entry[0] & 0xF) ^ 0x1))
    {
      break;
    } 
    if (read(disk, (void*)entry, 32) != 32)
    {
      fprintf_light_red(stderr, "Error while trying to read record.\n");
      return NULL;
    }
    *offset += 32;
  }
  return name;
}

char* read_short_entry(unsigned char* entry) 
{
    char* name = calloc(1, 12);
    memcpy(name, entry, 11);
    return name;
}

void fill_tm_from_fat32_timestamp(struct tm* result, uint16_t fat32_timestamp) 
{
    uint8_t seconds = (fat32_timestamp & 0x1F) << 1;
    uint8_t minutes = (fat32_timestamp >> 5) & 0x3F;
    uint8_t hours = (fat32_timestamp >> 11) & 0xF;
    
    result->tm_sec = (int) seconds;
    result->tm_min = (int) minutes;
    result->tm_hour = (int) hours;
}

void fill_tm_from_fat32_datestamp(struct tm* result, uint16_t fat32_date)
{
    uint8_t day = (uint8_t) (fat32_date & 0x1F);
    uint8_t month = (fat32_date >> 5) & 0xF;
    uint8_t year = (fat32_date >> 9) & 0x7F;
    
    result->tm_mday = (int) day;
    result->tm_mon = (int) (month - 1);
    result->tm_year = (1980 + (int) year) - 1900;
    result->tm_isdst = -1;
}

void remove_trailing_spaces(char* str) {
  int len = strlen(str); int last_space = 0;
  int i;
  bool seen_space = false;
  for (i = 0; i < len; i++) {
    if (str[i] == 0x20 && !seen_space) {
      seen_space = true;
      last_space = i;
    } else if (str[i] != 0x20) {
      seen_space = false;
    }
  }
  if (seen_space) str[last_space] = 0;
}

void remove_leading_spaces(char* str) {
  int i, len = strlen(str);
  for (i = 0; i < len; i++) {
    if (str[i] != 0x2e) {
      break;
    }
    str++;
  }
}

bool is_empty_extension(char* ext) {
  return !strcmp(ext, "   ");
}

bool is_dot_or_dotdot_entry(struct fat32_file* file_info) {
  return !strcmp(file_info->name, ".") || !strcmp(file_info->name, "..");
}

char* convert_short_filename(char* short_filename) {
  char* extension = short_filename + 8;
  char* name = calloc(1, 9); char* full_name;
  memcpy(name, short_filename, 8);
  extension = short_filename + 8;
  remove_trailing_spaces(name);
  if (!is_empty_extension(extension)) {
    remove_trailing_spaces(extension);
    full_name = calloc(1, 2 + strlen(extension) + strlen(name));
    strcpy(full_name, name);
    *(full_name + strlen(name)) = '.';
    strcpy(full_name + strlen(name) + 1, extension);
    return full_name;
  } 
  return name;
}

void print_file_info(struct fat32_file* file_info) {
    printf("name: %s\n", file_info->name);
    printf("path: %s\n", file_info->path);
    printf("is_dir: %s\n", file_info->is_dir ? "true" : "false");
    printf("cluster_num: %u\n", file_info->cluster_num);
    printf("dir_cluster_num: %u\n", file_info->dir_cluster_num);
    printf("dir_cluster_addr: %lu\n", file_info->dir_cluster_addr);
    printf("inode_sector: %lu\n", file_info->inode_sector);
    printf("inode_offset: %lu\n", file_info->inode_offset);
    printf("size: %u\n", file_info->size);
    printf("CRtime_unix: %s", ctime(&(file_info->crtime)));
    printf("LAtime_unix: %s", ctime(&(file_info->latime)));
    printf("LWtime_unix: %s", ctime(&(file_info->lwtime)));
}

void print_fat32_date(char* date_type, uint16_t date) {
    uint8_t day = (uint8_t) (date & 0x1F);
    uint8_t month = (date >> 5) & 0xF;
    uint8_t year = (date >> 9) & 0x7F;

    printf("%s: %u %u %u %u\n", date_type, date, year, month, day);
}

void print_fat32_timestamp(char* time_type, uint16_t fat32_timestamp)
{
    uint8_t seconds = (fat32_timestamp & 0x1F) << 1;
    uint8_t minutes = (fat32_timestamp >> 5) & 0x3F;
    uint8_t hours = (fat32_timestamp >> 11) & 0xF;
    
    printf("%s: %u %u %u %u\n", time_type, fat32_timestamp, hours, minutes,
           seconds);
}

void free_file_info(struct fat32_file* file_info) {
    free(file_info->name);
    free(file_info->path);
}

char* make_path_name(char* path, char* name) {
    char* file_path = calloc(1, strlen(path) + strlen(name) + 2);
    strcpy(file_path, path); 
    strcpy(file_path + strlen(path), "/");
    strcpy(file_path + strlen(path) + 1, name);
    return file_path;
}

void fat32_reset_file_info(struct fat32_file* file_info)
{
    file_info->name = NULL;
    file_info->path = NULL;
    file_info->cluster_num = 0;
    file_info->inode_sector = 0;
    file_info->inode_offset = 0;
    file_info->size = 0;
    file_info->crtime = 0;
    file_info->latime = 0;
    file_info->lwtime = 0;
}

int fat32_serialize_file_info(struct fs* fs, int disk, struct fat32_file* file, 
                              int serializef, struct bson_info* prev_dir_files,
                              struct bson_info* cur_dir_files)
{
    struct bson_info* serialized;
    struct bson_info* sectors;
    struct bson_kv value;
    struct bson_kv sector_value;
    struct bson_kv dentry;

    char count[32];

    uint64_t counter = 0;
    uint64_t cluster_num = file->cluster_num;
    uint64_t cluster_sector = 0;
    uint64_t inode_sector = file->inode_sector;
    uint64_t inode_offset = file->inode_offset;
    uint64_t inode_num = file->inode_num;
    uint64_t size = file->size;
    uint64_t mode = 0;
    uint64_t link_count = 1;
    uint64_t uid = 0;
    uint64_t gid = 0;
    uint64_t atime = file->latime;
    uint64_t mtime = file->lwtime;
    uint64_t ctime = file->crtime;

    uint8_t file_entry_buf[4096 + sizeof(uint64_t)];
    char dentry_key[32];
  
    serialized = bson_init();
    sectors = bson_init();

    value.type = BSON_STRING;
    value.size = strlen("file");
    value.key = "type";
    value.data = "file";

    bson_serialize(serialized, &value);

    value.type = BSON_INT64;
    value.key = "inode_sector";
    value.data = &inode_sector;

    bson_serialize(serialized, &value);

    value.type = BSON_INT64; 
    value.key = "inode_offset";
    value.data = &inode_offset;

    bson_serialize(serialized, &value);

    value.type = BSON_INT32; 
    value.key = "inode_num";
    value.data = &inode_num;

    bson_serialize(serialized, &value);

    value.type = BSON_STRING;
    value.size = strlen(file->path);
    value.key = "path";
    value.data = file->path;

    bson_serialize(serialized, &value);

    value.type = BSON_BOOLEAN;
    value.key = "is_dir";
    value.data = &(file->is_dir);

    bson_serialize(serialized, &value);

    value.type = BSON_INT64; 
    value.key = "size";
    value.data = &size;

    bson_serialize(serialized, &value);

    if (file->is_dir) {
      mode = S_IFDIR | 0755;
    } else {
      mode = S_IFREG | 0755;
    }

    value.type = BSON_INT64; 
    value.key = "mode";
    value.data = &mode;

    bson_serialize(serialized, &value);

    value.type = BSON_INT64; 
    value.key = "link_count";
    value.data = &link_count;

    bson_serialize(serialized, &value);

    value.type = BSON_INT64; 
    value.key = "uid";
    value.data = &uid;

    bson_serialize(serialized, &value);

    value.type = BSON_INT64; 
    value.key = "gid";
    value.data = &gid;

    bson_serialize(serialized, &value);

    value.type = BSON_INT64; 
    value.key = "atime";
    value.data = &atime;

    bson_serialize(serialized, &value);

    value.type = BSON_INT64; 
    value.key = "mtime";
    value.data = &mtime;

    bson_serialize(serialized, &value);

    value.type = BSON_INT64; 
    value.key = "ctime";
    value.data = &ctime;

    bson_serialize(serialized, &value);

    value.type = BSON_ARRAY;
    value.key = "sectors";
    value.data = sectors;

    while(cluster_num != FAT32_EOC &&
          cluster_num != FAT32_BPB)
    {
        cluster_sector = get_cluster_addr(fs, cluster_num) / SECTOR_SIZE;
        cluster_num = get_fat_entry(disk, cluster_num, fs);
        snprintf(count, 32, "%"PRIu64, counter);

        sector_value.key = count;
        sector_value.type = BSON_INT64;
        sector_value.data = &cluster_sector;

        bson_serialize(sectors, &sector_value);

        counter += 1;
    }

    bson_finalize(sectors);
    bson_serialize(serialized, &value);

    if (prev_dir_files != NULL) {
      snprintf(dentry_key, 32, "%"PRIu64, inode_sector);
      dentry.key = dentry_key;
      dentry.type = BSON_BINARY;
      dentry.data = file_entry_buf;
      dentry.size = sizeof(uint64_t) + strlen(file->name);
      memcpy(file_entry_buf, &inode_num, sizeof(uint64_t));
      memcpy(&(file_entry_buf[sizeof(uint64_t)]), file->name, strlen(file->name));
      bson_serialize(prev_dir_files, &dentry);
    }

    if (cur_dir_files != NULL) {
      value.type = BSON_ARRAY;
      value.key = "files";
      value.data = cur_dir_files;
      bson_finalize(cur_dir_files);
      bson_serialize(serialized, &value);
    }

    bson_finalize(serialized);
    bson_writef(serialized, serializef);
    bson_cleanup(serialized);
    bson_cleanup(sectors);
    if (cur_dir_files) {
      bson_cleanup(cur_dir_files);
    }
    return 0;
}


int read_dir_cluster(char* path, int disk, uint32_t cluster_num,
                     struct fs* fs, struct bson_info* prev_dir_files, int serializef)
{
    uint64_t cluster_addr = get_cluster_addr(fs, cluster_num);
    int offset = 0;
    unsigned char* entry = calloc(1, 32); 
    struct fat32_volumeID* volID = fs->fs_info;
    char* long_name = NULL;
    struct fat32_file file_info = {0};
    static uint64_t inode_num = 1; //root is zero, root_dot is one
    if (lseek64(disk, (off64_t) (cluster_addr), SEEK_SET) == (off64_t) -1)
    {
        fprintf_light_red(stderr, "Failed seeking to cluster_addr: "
                                  "%"PRIu64"\n", (uint64_t) cluster_addr);
        return -1;
    }

    while (true) 
    {
        if (read(disk, (void*)entry, 32) != 32)
        {
            fprintf_light_red(stderr, "Error while trying to read record.\n");
            return -1;
        }
        
        offset += 32;

        if (!(entry[0] ^ (unsigned char) 0xe5))
        {
            // This entry is empty.
            continue;
        }

        if (!entry[0]) 
        {
            // No more directory entries.
            break;
        }

        if (!(entry[11] ^ (unsigned char) 0x08))
        {
            // This entry is the volume id.
            continue;
        }

        if (file_info.inode_sector == 0) 
        {
            file_info.dir_cluster_num = cluster_num;
            file_info.dir_cluster_addr = cluster_addr;
            file_info.inode_sector = cluster_addr / SECTOR_SIZE;
            file_info.inode_offset = offset;
        }


        if (!(entry[11] ^ (unsigned char) 0xF)) 
        {
            char* entry_name = read_name_long_entry(entry);

            if (!long_name) 
            {
                long_name = entry_name;
            }
            else 
            {
                char* temp = long_name;
                long_name = calloc(1, strlen(temp) + strlen(entry_name) + 1);
                strcpy(long_name, entry_name); strcat(long_name, temp);
            }
        } 
        else 
        {
            char* short_name = read_short_entry(entry);

            if (long_name) 
            {
                file_info.name = long_name;
                remove_trailing_spaces(file_info.name);
                long_name = NULL;
            }
            else 
            {
                file_info.name = convert_short_filename(short_name);
                hexdump((uint8_t*)short_name, strlen(short_name));
                printf("NO LONG NAME %s\n", file_info.name);
            }

            
            file_info.inode_num = inode_num;
            inode_num++;
            uint8_t crtime_tenth = *((uint8_t*) (entry + 13));
            uint16_t crtime = *((uint16_t*) (entry + 14));
            uint16_t crdate = *((uint16_t*) (entry + 16));
            uint16_t ladate = *((uint16_t*) (entry + 18));
            uint16_t lwtime = *((uint16_t*) (entry + 22));
            uint16_t lwdate = *((uint16_t*) (entry + 24));
            struct tm crinfo = {0};
            struct tm lainfo = {0};
            struct tm lwinfo = {0};
            fill_tm_from_fat32_datestamp(&crinfo, crdate);
            fill_tm_from_fat32_timestamp(&crinfo, crtime);
            crinfo.tm_sec += (int) (crtime_tenth / 100);
            time_t created_unix_time = mktime(&crinfo);

            fill_tm_from_fat32_datestamp(&lainfo, ladate);
            time_t lastaccessed_unix_time = mktime(&lainfo);

            fill_tm_from_fat32_datestamp(&lwinfo, lwdate);
            fill_tm_from_fat32_timestamp(&lwinfo, lwtime);
            time_t lastwritten_unix_time = mktime(&lwinfo);
            
            file_info.crtime = created_unix_time;
            file_info.latime = lastaccessed_unix_time;
            file_info.lwtime = lastwritten_unix_time;
            file_info.size = *((uint32_t*) (entry + 28));
            uint32_t cluster_hi1 = ((uint32_t) entry[20]) << 16;
            uint32_t cluster_hi2 = ((uint32_t) entry[21]) << 24;
            uint32_t cluster_lo1 = ((uint32_t) entry[26]);
            uint32_t cluster_lo2 = (uint32_t) entry[27] << 8;
            uint32_t file_cluster_num = cluster_hi1 | cluster_hi2 |
                                        cluster_lo1 | cluster_lo2;
            file_info.cluster_num = file_cluster_num;
            file_info.path = make_path_name(path, file_info.name);
            
            struct bson_info* cur_dir_files = NULL;

            if ((entry[11] & (unsigned char)0x10) && entry[0] ^
                (unsigned char)0x2E)  
            {
                file_info.is_dir = true;
                cur_dir_files = bson_init();
                print_file_info(&file_info);
                read_dir_cluster(file_info.path, disk, file_cluster_num, fs,
                                 cur_dir_files, serializef);
                lseek64(disk, (off64_t) (cluster_addr + offset), SEEK_SET);
            }
            else
            {
                file_info.is_dir = false;
                print_file_info(&file_info);
            }
            if (!is_dot_or_dotdot_entry(&file_info)) {
              fat32_serialize_file_info(fs, disk, &file_info, serializef,
                                      prev_dir_files, cur_dir_files);
            }
            free_file_info(&file_info);
            fat32_reset_file_info(&file_info);
        }

        if (offset == SECTOR_SIZE * volID->sectors_per_cluster) 
        {
            uint32_t fat_entry = get_fat_entry(disk, cluster_num, fs);

            if (fat_entry == FAT32_EOC) 
            {
                printf("End of Directory! (fat_entry) \n");
                return 0;
            }
            cluster_num = fat_entry;
            printf("fat_entry %" PRIu32 "\n", fat_entry);
            cluster_addr = get_cluster_addr(fs,fat_entry);
            printf("cluster_addr %" PRId64 "\n", cluster_addr);
            offset = 0;
            lseek64(disk, (off64_t) (cluster_addr), SEEK_SET);
        }
    }

    free(entry);

    return 0;
}

int fat32_serialize_fs(struct fs* fs, int serializef)
{
    struct bson_info* serialized;
    struct bson_kv value;
    int64_t partition_offset = fs->pt_off;
    struct fat32_volumeID* volid = (struct fat32_volumeID*) fs->fs_info;
    int32_t num_block_groups = volid->num_fats;
    int32_t num_files = -1;
    uint64_t block_size = volid->bytes_per_sector * volid->sectors_per_cluster;
    uint64_t blocks_per_group = (volid->sectors_per_fat *
                                 volid->bytes_per_sector) / block_size;
    uint64_t inode_size = 0;
    uint64_t inodes_per_group = 0;

    serialized = bson_init();

    value.type = BSON_STRING;
    value.size = strlen("fs");
    value.key = "type";
    value.data = "fs";

    bson_serialize(serialized, &value);

    value.type = BSON_INT32;
    value.key = "pte_num";
    value.data = &(fs->pte);

    bson_serialize(serialized, &value);

    value.type = BSON_STRING;
    value.size = strlen("fat32");
    value.key = "fs";
    value.data = "fat32";

    bson_serialize(serialized, &value);

    value.type = BSON_STRING;
    value.key = "mount_point";
    value.size = strlen("/");
    value.data = "/";

    bson_serialize(serialized, &value);

    value.type = BSON_INT32;
    value.key = "num_block_groups";
    value.data = &(num_block_groups);

    bson_serialize(serialized, &value);

    value.type = BSON_INT32;
    value.key = "num_files";
    value.data = &(num_files);

    bson_serialize(serialized, &value);

    value.type = BSON_INT64;
    value.key = "superblock_sector";
    partition_offset /= SECTOR_SIZE;
    value.data = &(partition_offset);

    bson_serialize(serialized, &value);

    value.type = BSON_INT64;
    value.key = "superblock_offset";
    partition_offset %= SECTOR_SIZE;
    value.data = &(partition_offset);

    bson_serialize(serialized, &value);

    value.type = BSON_INT64;
    value.key = "block_size";
    value.data = &(block_size);

    bson_serialize(serialized, &value);

    value.type = BSON_INT64;
    value.key = "blocks_per_group";
    value.data = &(blocks_per_group);

    bson_serialize(serialized, &value);

    value.type = BSON_INT64;
    value.key = "inodes_per_group";
    value.data = &(inodes_per_group);

    bson_serialize(serialized, &value);

    value.type = BSON_INT64;
    value.key = "inode_size";
    value.data = &(inode_size);

    bson_serialize(serialized, &value);

    bson_finalize(serialized);
    bson_writef(serialized, serializef);
    bson_cleanup(serialized);

    return 0;
}

int fat32_serialize(int disk, struct fs* fs, int serializef)
{
    struct fat32_file root = {0};
    struct bson_info* root_files = bson_init();
    
    fat32_serialize_fs(fs, serializef);

    root.is_dir = true;
    root.inode_num = 0;
    root.cluster_num = 2;
    root.path = "/";

    /*struct fat32_file root_dot = {0};
    root_dot.name = ".";
    root_dot.path = "/.";
    root_dot.is_dir = true;
    root_dot.cluster_num = 2;
    root_dot.inode_num = 1;
    root_dot.inode_sector = get_cluster_addr(fs, 2) / SECTOR_SIZE;*/

    /* serialize root file system depth-first */
    if (read_dir_cluster("", disk, 2, fs, root_files, serializef))
    {
        fprintf_light_red(stderr, "Error reading dir cluster 2.\n");
        return -1;
    }
    //fat32_serialize_file_info(fs, disk, &root_dot, serializef, root_files, NULL);
    fat32_serialize_file_info(fs, disk, &root, serializef, NULL, root_files);

    return 0;
}

int fat32_cleanup(struct fs* fs)
{ 
    if (fs->fs_info)
        free(fs->fs_info);
    return 0;
}

