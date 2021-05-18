#include "fat32.h"
#include <printf.h>
#include <uart.h>
#include <string.h>
#include <varied.h>
#include <vfs.h>
#include <sdhost.h>

static int lookup(struct vnode* dir_node, struct vnode** target, const char* component_name) {
  struct directory_entry* d_entry, *find_entry;
  char buf[FAT32_BLOCK_SIZE], fat_table[FAT32_BLOCK_SIZE], filename[FAT32_D_ENTRY_NAME_SIZE + FAT32_D_ENTRY_EXTENSION_SIZE + 1];
  size_t cluster_num, pre_cluster_num;
  int max_len;
  
  if(component_name == null)
    return 1;
  if(dir_node == null)
    return 1;

  d_entry = dir_node->internal;

  //is directory
  if((d_entry->attribute) & 0x10 == 0) 
    return 1;

  //get first cluster number
  cluster_num = (d_entry->start_cluster_high << 16) + d_entry->start_cluster_low;
  pre_cluster_num = cluster_num;
  //read first fat table
  readblock(current_partition->p_entry->lba + current_partition->boot_sector->num_of_reserved_sectors + cluster_num / FAT32_ENTRY_PER_FAT_TABLE, fat_table);
   
  while(1) {
    //read directory table in this cluster
    readblock(current_partition->p_entry->lba + current_partition->boot_sector->num_of_reserved_sectors + current_partition->boot_sector->sectors_per_fat_large_fat32 * 2 + cluster_num - 2, buf);
    
    d_entry = (struct directory_entry *)buf;
    printf("cluster_num: %d\n", cluster_num);
    //traverse directory table
    while(d_entry <= buf + FAT32_BLOCK_SIZE) {
      if(*((char*)d_entry) == '\x00') {
        //dir end
        break;
      }
      else if(*((char*)d_entry) == '\xe5') {
        //unused
      }
      //Long filename text - Attrib has all four type bits set
      else {
        //sfn directory entry
        memset(filename, '\x00', FAT32_D_ENTRY_NAME_SIZE + FAT32_D_ENTRY_EXTENSION_SIZE + 1);
        strncpy(filename, d_entry->name, FAT32_D_ENTRY_NAME_SIZE);
        strncpy(filename + strlen(filename), d_entry->extension, FAT32_D_ENTRY_EXTENSION_SIZE);
        printf("name: %s, size: %x, attribute: %x, first cluster: %x\n", filename, d_entry->size,  d_entry->attribute, cluster_num);
        
        //compare component name
        max_len = strlen(filename);
        if(max_len < strlen(component_name)) 
          max_len = strlen(component_name);
        if(strncmp(d_entry->name, component_name, max_len) == 0) {
          //*target = ;
          return 0;
        }
      }
      d_entry += 1;
    }
    //check if need to read next fat table
    if(pre_cluster_num / FAT32_D_ENTRY_PER_D_TABLE != cluster_num / FAT32_D_ENTRY_PER_D_TABLE) {
      //need to read fat table
      readblock(current_partition->p_entry->lba + current_partition->boot_sector->num_of_reserved_sectors + cluster_num / FAT32_ENTRY_PER_FAT_TABLE, fat_table);
    }
    
    //read next cluster
    pre_cluster_num = cluster_num;
    cluster_num = *(uint32_t *)(fat_table + (cluster_num % 128) * 4);
    
    //check if is last cluster
    if(IS_EOC(cluster_num)) {
      break;
    }
  }

  return 1;
}

static int setup_mount(struct filesystem* fs, struct mount* _mount) {
  struct directory_entry *d_entry;
  _mount->fs = fs;
  _mount->root = fat32_vnode_create(_mount);

  d_entry = (struct directory_entry* )varied_malloc(sizeof(struct directory_entry));

  if(d_entry == null) 
    return -1;

  //set root directory entry
  strncpy(d_entry->name, "/", 1);
  d_entry->start_cluster_high = 0;
  d_entry-> start_cluster_low = 2;
  d_entry->attribute = 0x10;
  
  _mount->root->internal = d_entry;
  if(_mount->root == null) {
    return null;
  }
  
  struct vnode* v_node = (struct vnode *)varied_malloc(sizeof(struct vnode));
  lookup(_mount->root, &v_node, "rootfs");
        
  return 0;
}

void* fat32_vnode_create(struct mount* _mount) {
  struct vnode* v_node;
  
  v_node = (struct vnode* )varied_malloc(sizeof(struct vnode));

  if(v_node == null)
    return null;
  
  v_node->mount = _mount;
  v_node->f_ops = &fat32_fops;
  v_node->v_ops = &fat32_vops;

  //set internal to root directory entry
  v_node->internal = current_partition->d_table->root_entry;
  
  return v_node;
}

static int write(struct file* file, const void* buf, size_t len) {
  return 0;
}

static int read(struct file* file, void* buf, size_t len) {
  return 0;
}


static int create(struct vnode* dir_node, struct vnode** target, const char* component_name) {
  return 0;
}

void fat32_init() {
  fat32_parse_mbr();

  for(int i = 0; i < 4; i++) {
    if(_mbr.partitions[i].lba != 0) {
      
      fat32_info_list[i].p_entry = &_mbr.partitions[i];
      fat32_info_list[i].boot_sector = fat32_parse_boot_sector(_mbr.partitions[i].lba);
      
      if(fat32_info_list[i].boot_sector != null) {
        fat32_parse_root_directory(&fat32_info_list[i]);
        fat32_traverse_root_directory(&fat32_info_list[i]);
        current_partition = &fat32_info_list[i];
        //test_read_file1(&fat32_info_list[i]);
      }
    }
  }
  
  //regist
  fat32_fops.read = read;
  fat32_fops.write = write;
  fat32_vops.create = create;
  fat32_vops.lookup = lookup;
  
  struct filesystem fat32_fs;
  fat32_fs.name = "fat32";
  fat32_fs.setup_mount = setup_mount;

  register_filesystem(&fat32_fs);  
}

void fat32_parse_mbr() {
  char buf[FAT32_BLOCK_SIZE];
  
  readblock(0, buf);
  //copy mbr
  memcpy((char *)&_mbr, buf, sizeof(_mbr));

  strncpy(buf, (char *)_mbr.signature, 2);
  buf[3] = '\0';

  printf("mbr: \n\
  addr: %x\n\
  partition 1: type code: %x, lba: %x: num_of_sectors: %x\n\
  partition 2: type code: %x, lba: %x: num_of_sectors: %x\n\
  partition 3: type code: %x, lba: %x: num_of_sectors: %x\n\
  partition 4: type code: %x, lba: %x: num_of_sectors: %x\n\
  signature1: %x\n\
  signature2: %x\n",
  &_mbr,
  _mbr.partitions[0].type_code, _mbr.partitions[0].lba, _mbr.partitions[0].num_of_sectors,
  _mbr.partitions[1].type_code, _mbr.partitions[1].lba, _mbr.partitions[1].num_of_sectors,
  _mbr.partitions[2].type_code, _mbr.partitions[2].lba, _mbr.partitions[2].num_of_sectors,
  _mbr.partitions[3].type_code, _mbr.partitions[3].lba, _mbr.partitions[3].num_of_sectors,
  buf[0],
  buf[1]);
}

void* fat32_parse_boot_sector(uint32_t lba) {
  char buf[FAT32_BLOCK_SIZE];
  
  struct boot_sector *boot_sector;

  readblock(lba, buf);
  
  boot_sector = (struct boot_sector*)varied_malloc(sizeof(struct boot_sector));

  if(boot_sector == null) 
    return null;
  
  //printf("size: %d\n", sizeof(struct boot_sector));
  memcpy((char*)boot_sector, buf, sizeof(struct boot_sector));
  
  //boot_sector = buf;
  printf("boot_sector: \n\
  addr: %x\n\
  char oem_name[8]: %s\n\
  uint16_t bytes_per_sector: %d\n\
  uint8_t sectors_per_cluster: %d\n\
  uint16_t num_of_reserved_sectors: %d\n\
  uint8_t num_of_fats: %d\n\
  uint16_t max_root_entries: %d\n\
  uint16_t num_of_sectors: %d\n\
  uint8_t media_descriptor: %d\n\
  uint16_t sectors_per_fat: %d\n\
  uint16_t sectors_per_track: %d\n\
  uint16_t num_of_head: %d\n\
  uint32_t num_of_hidden_sectors: %d\n\
  uint32_t num_of_sectors_large: %d\n\
  uint32_t sectors_per_fat_large_fat32: %d\n\
  uint16_t flag: %x\n\
  uint16_t version: %d\n\
  uint32_t cluster_num_of_root_dir: %d\n\ 
  uint16_t num_of_fs_info_sectors: %d\n\
  uint16_t num_of_backup_sectors: %d\n\
  uint8_t drvie_num: %d\n",
  boot_sector,
  boot_sector->oem_name,
  boot_sector->bytes_per_sector,
  boot_sector->sectors_per_cluster,
  boot_sector->num_of_reserved_sectors,
  boot_sector->num_of_fats,
  boot_sector->max_root_entries,
  boot_sector->num_of_sectors,
  boot_sector->media_descriptor,
  boot_sector->sectors_per_fat,
  boot_sector->sectors_per_track,
  boot_sector->num_of_head,
  boot_sector->num_of_hidden_sectors,
  boot_sector->num_of_sectors_large,
  boot_sector->sectors_per_fat_large_fat32,
  boot_sector->flag,
  boot_sector->version,
  boot_sector->cluster_num_of_root_dir,
  boot_sector->sector_num_of_fs_info_sector,
  boot_sector->sector_num_of_backup_sector,
  boot_sector->drvie_num);
  
  printf("addr: %x\n", buf);
  readblock(lba + boot_sector->num_of_reserved_sectors, buf);
  return boot_sector;
}

void fat32_parse_root_directory(struct fat32_info* fat32_info) {
  char buf[FAT32_BLOCK_SIZE];
  struct directory_entry* d_entry;
  struct directory_table* d_table;
  char filename[FAT32_D_ENTRY_NAME_SIZE + FAT32_D_ENTRY_EXTENSION_SIZE + 1];
  int i;
  
  //root directory
  readblock(fat32_info->p_entry->lba + fat32_info->boot_sector->num_of_reserved_sectors + fat32_info->boot_sector->sectors_per_fat_large_fat32 * 2, buf);
  
  i = 0;
  while(1) {
    
    if(*(buf + i) == '\x00') {
      //dir end
      i += FAT32_D_ENTRY_SIZE;
      break;
    }
    else if(*(buf + i) == '\xe5') {
      //unused
    }
    //Long filename text - Attrib has all four type bits set
    else {
    }
    
    i += FAT32_D_ENTRY_SIZE;
  }
  
  d_entry = (struct directory_entry* )varied_malloc(i);

  if(d_entry == null)
    return;
  
  memcpy(d_entry, buf, i);
  
  d_table = (struct directory_table* )varied_malloc(sizeof(struct directory_table));

  if(d_table == null)
    return;

  d_table->root_entry = d_entry;
  d_table->size = i;

  fat32_info->d_table = d_table;
}

void fat32_traverse_root_directory(struct fat32_info* fat32_info) {
  char buf[FAT32_BLOCK_SIZE];
  struct directory_entry* d_entry;
  char filename[FAT32_D_ENTRY_NAME_SIZE + FAT32_D_ENTRY_EXTENSION_SIZE + 1];
  
  size_t cluster_num;
  d_entry = fat32_info->d_table->root_entry;
  while(1) {
    
    if(*((char*)d_entry) == '\x00') {
      //dir end
      break;
    }
    else if(*((char*)d_entry) == '\xe5') {
      //unused
    }
    //Long filename text - Attrib has all four type bits set
    else {
      memset(filename, '\x00', FAT32_D_ENTRY_NAME_SIZE + FAT32_D_ENTRY_EXTENSION_SIZE + 1);
      strncpy(filename, d_entry->name, FAT32_D_ENTRY_NAME_SIZE);
      strncpy(filename + strlen(filename), d_entry->extension, FAT32_D_ENTRY_EXTENSION_SIZE);
      cluster_num = (d_entry->start_cluster_high << 16) +  d_entry->start_cluster_low;
      printf("d_entry:\nname: %s, size: %x, attribute: %x, first cluster: %x\n", d_entry->name, d_entry->size,  d_entry->attribute, cluster_num);
    }
    
    d_entry += 1;
  }
 }

void test_read_file1(struct fat32_info * _fat32_info) {
  char buf[FAT32_BLOCK_SIZE];
  char fat_table[FAT32_BLOCK_SIZE];

  struct directory_entry* d_entry;
  char filename[FAT32_D_ENTRY_NAME_SIZE + FAT32_D_ENTRY_EXTENSION_SIZE + 1];
  size_t cluster_num;

  //first cluster of file
  cluster_num = (_fat32_info->d_table->root_entry[4].start_cluster_high << 16) + _fat32_info->d_table->root_entry[4].start_cluster_low;
  //while(1) {
    //read next cluster num in fat table
    
    //reuse fat table if in same cluster
    
    readblock(_fat32_info->p_entry->lba + _fat32_info->boot_sector->num_of_reserved_sectors + cluster_num / 128, fat_table);
    cluster_num = *(uint32_t *)(fat_table + (cluster_num % 128) * 4);
    //printf("cluster_num: %x, addr: %x\n", cluster_num, buf);
    //printf("%s\n", buf);
    d_entry = buf;
    while(1) {
      
      if(*((char*)d_entry) == '\x00') {
        //dir end
        break;
      }
      else if(*((char*)d_entry) == '\xe5') {
        //unused
      }
      //Long filename text - Attrib has all four type bits set
      else {
        memset(filename, '\x00', FAT32_D_ENTRY_NAME_SIZE + FAT32_D_ENTRY_EXTENSION_SIZE + 1);
        strncpy(filename, d_entry->name, FAT32_D_ENTRY_NAME_SIZE);
        strncpy(filename + strlen(filename), d_entry->extension, FAT32_D_ENTRY_EXTENSION_SIZE);
        cluster_num = (d_entry->start_cluster_high << 16) +  d_entry->start_cluster_low;
        printf("name: %s, size: %x, attribute: %x, first cluster: %x\n", filename, d_entry->size,  d_entry->attribute, cluster_num);
      }
      
      d_entry += 1;
    }

    readblock(_fat32_info->p_entry->lba + _fat32_info->boot_sector->num_of_reserved_sectors +  _fat32_info->boot_sector->sectors_per_fat_large_fat32 * 2  + 0x000025C8 - 2, buf);
    printf("%s\n", buf);
    //if(IS_EOC(cluster_num))
      //break;
    

 // }
  //root directory
  //readblock(fat32_info->p_entry->lba + fat32_info->boot_sector->num_of_reserved_sectors + fat32_info->boot_sector->sectors_per_fat_large_fat32 * 2, buf);

}