#include "cache.h"

/*
 * 检查 sha1 值对应的文件是否存在且可以读取
 */
static int check_valid_sha1(unsigned char *sha1)
{
	/* 将 sha1 值转换成相应的文件名 */
	char *filename = sha1_file_name(sha1);
	int ret;

	/* If we were anal, we'd check that the sha1 of the contents actually matches */
	/* 检查对文件 filename 是否有读权限 (R_OK) */
	ret = access(filename, R_OK);
	if (ret)
		perror(filename);
	return ret;
}

/* 将数值 val 填充到 buffer[i] 往前的位置, 并返回开始位置值 */
static int prepend_integer(char *buffer, unsigned val, int i)
{
	/* value = 54321, buffer[] = "____54321\0______"
	 *                                      |
	 *                                      i
	 */
	buffer[--i] = '\0';
	do {
		buffer[--i] = '0' + (val % 10);
		val /= 10;
	} while (val);
	return i;
}

#define ORIG_OFFSET (40)	/* Enough space to add the header of "tree <size>\0" */

int main(int argc, char **argv)
{
	unsigned long size, offset, val;
	/* 读取索引文件".dircache/index"到内存, 建立缓存, 返回条目数 */
	int i, entries = read_cache();
	char *buffer;

	if (entries <= 0) {
		fprintf(stderr, "No file-cache to create a tree of\n");
		exit(1);
	}

	/* Guess at an initial size */
	size = entries * 40 + 400;
	buffer = malloc(size);
	offset = ORIG_OFFSET;

	/* 遍历每一个条目 */
	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = active_cache[i];
		/* 根据 cache entry 的 sha1 值检查对应的文件是否存在且可读取 */
		if (check_valid_sha1(ce->sha1) < 0)
			exit(1);
		if (offset + ce->namelen + 60 > size) {
			size = alloc_nr(offset + ce->namelen + 60);
			buffer = realloc(buffer, size);
		}
		offset += sprintf(buffer + offset, "%o %s", ce->st_mode, ce->name);
		buffer[offset++] = 0;
		memcpy(buffer + offset, ce->sha1, 20);
		offset += 20;
	}

	/* 将数值 offset - ORIG_OFFSET 填充到 buffer[ORIG_OFFSET] 往前的位置, 并返回开始位置值 */
	i = prepend_integer(buffer, offset - ORIG_OFFSET, ORIG_OFFSET);
	i -= 5;
	/* 填充 "tree " 字符串, 从 "tree " 开始到结束实际上是一个 tree 对象 */
	memcpy(buffer+i, "tree ", 5);

	buffer += i;
	offset -= i;

	/* 将 tree 对象数据写入到 objects 中 */
	write_sha1_file(buffer, offset);
	return 0;
}

/* #
 * # write-tree 使用示例
 * #
 *
 */