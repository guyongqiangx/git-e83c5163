#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/mman.h>

#include <openssl/sha.h>
#include <zlib.h>
#include <string.h> /* memcmp,memmove,memset,memcpy,strlen */

/*
 * Basic data structures for the directory cache
 *
 * NOTE NOTE NOTE! This is all in the native CPU byte format. It's
 * not even trying to be portable. It's trying to be efficient. It's
 * just a cache, after all.
 */

/* 暂存区文件(".dircache/index")头部格式 */
#define CACHE_SIGNATURE 0x44495243	/* "DIRC" */
struct cache_header {
	unsigned int signature;
	unsigned int version;
	unsigned int entries;
	unsigned char sha1[20];
};

/*
 * The "cache_time" is just the low 32 bits of the
 * time. It doesn't matter if it overflows - we only
 * check it for equality in the 32 bits we save.
 */
struct cache_time {
	unsigned int sec;
	unsigned int nsec;
};

/*
 * dev/ino/uid/gid/size are also just tracked to the low 32 bits
 * Again - this is just a (very strong in practice) heuristic that
 * the inode hasn't changed.
 */
/*
 * 暂存区文件(".dircache/index")内单条记录格式
 * 最后一个 name 成员是大小为0的数组(用作占位符, 具体依赖于名字长度),
 * 一条 cache entry 由前面定长的部分和最后变长的部分构成, 所以总体长度不是固定的
 */
struct cache_entry {
	/* 固定长度部分 */
	struct cache_time ctime;
	struct cache_time mtime;
	unsigned int st_dev;
	unsigned int st_ino;
	unsigned int st_mode;
	unsigned int st_uid;
	unsigned int st_gid;
	unsigned int st_size;
	unsigned char sha1[20];
	unsigned short namelen;
	/* 每条 cache entry 记录的 name 实际占用的长度由前面的 namelen 决定, 这里的 name[0] 就是一个大小为0的占位符 */
	unsigned char name[0];
};

/* 用于存储版本库目录路径, 默认为 ".dircache/objects", 实际没有使用, 每次都重新设置 */
const char *sha1_file_directory;
/* 内存中的 cache entry 缓存数组指针 */
struct cache_entry **active_cache = NULL;
/*
 *    active_nr: 为暂存区文件 ".dircache/index" 包含的实际 cache entry 数目
 * active_alloc: 为内存中可存放的 cache entry 数目(前面 cative_nr 部分已使用)
 */
unsigned int active_nr = 0, active_alloc = 0;

/*
 * 版本库目录的环境变量和默认名称, 当前git版本库目录已经改为".git/objects"了
 */
#define DB_ENVIRONMENT "SHA1_FILE_DIRECTORY"
#define DEFAULT_DB_ENVIRONMENT ".dircache/objects"

/*
 * 根据传入name字符串的长度len, 计算所在cache entry的大小(8字节对齐)
 */
#define cache_entry_size(len) ((offsetof(struct cache_entry,name) + (len) + 8) & ~7)
/*
 * 根据传入cache entry的记录, 计算cache entry的大小
 */
#define ce_size(ce) cache_entry_size((ce)->namelen)

#define alloc_nr(x) (((x)+16)*3/2)

/* Initialize the cache information */
/* 读取索引文件".dircache/index", 建立缓存, 返回条目数 */
extern int read_cache(void);

/* Return a statically allocated filename matching the sha1 signature */
/* 获取 sha1 值对应的文件名 */
extern char *sha1_file_name(unsigned char *sha1);

/* Write a memory buffer out to the sha file */
/* 将 buf 中大小为 size 的数据写入到 sha1 值对应的文件中 */
extern int write_sha1_buffer(unsigned char *sha1, void *buf, unsigned int size);

/* Read and unpack a sha1 file into memory, write memory to a sha1 file */
/* 提取 sha1 值对应文件的内容(解压缩后返回), 返回内容类型(blob/tree/commit)和 size */
extern void * read_sha1_file(unsigned char *sha1, char *type, unsigned long *size);
/* 压缩 buf 数据, 计算 sha1 值, 并写入对应的 sha1 文件中 */
extern int write_sha1_file(char *buf, unsigned len);

/* Convert to/from hex/sha1 representation */
/* 将 sha1 字符串转换成相应的 sha1 值 */
extern int get_sha1_hex(char *hex, unsigned char *sha1);
/* 将 sha1 值转换成相应的 sha1 字符串 */
extern char *sha1_to_hex(unsigned char *sha1);	/* static buffer! */

/* General helper functions */
extern void usage(const char *err);

#endif /* CACHE_H */
