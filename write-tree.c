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

	/* 将 tree 对象数据写入到文件中 */
	write_sha1_file(buffer, offset);
	return 0;
}

/* #
 * # write-tree 使用示例
 * #
 *
 * # 1. 查看 .dircache/objects 下的对象 (有两个对象, 分别对应 Makefile 和 README 数据)
 * git-e83c5163$ tree .dircache/ -a
 * .dircache/
 * ├── index
 * └── objects
 *     ├── 00
 *     ...
 *     ├── 66
 *     │   └── 5025b11ce8fb16fadb7daebf77cb54a2ae39a1
 *     ...
 *     ├── b0
 *     │   └── 4fb99b9a176ff05e03d5e6e739f0a82b83c56c
 *     ...
 *     └── ff
 *
 * 257 directories, 3 files
 *
 * # 2. 查看 .dircache/index 暂存的内容(包含 Makefile 和 README 两个文件)
 * git-e83c5163$ xxd -g 1 .dircache/index
 * 00000000: 43 52 49 44 01 00 00 00 02 00 00 00 4f 1d b5 af  CRID........O...
 * 00000010: 48 e0 35 5d 65 c2 23 f2 0d 02 2d 4c 83 6b 1d 16  H.5]e.#...-L.k..
 * 00000020: 10 7b ed 60 b6 28 26 27 10 7b ed 60 b6 28 26 27  .{.`.(&'.{.`.(&'
 * 00000030: 03 fc 00 00 86 83 e8 03 a4 81 00 00 8b 63 00 00  .............c..
 * 00000040: 14 00 00 00 ca 03 00 00 b0 4f b9 9b 9a 17 6f f0  .........O....o.
 * 00000050: 5e 03 d5 e6 e7 39 f0 a8 2b 83 c5 6c 08 00 4d 61  ^....9..+..l..Ma
 * 00000060: 6b 65 66 69 6c 65 00 00 31 79 ed 60 0e eb ae 26  kefile..1y.`...&
 * 00000070: 31 79 ed 60 0e eb ae 26 03 fc 00 00 87 83 e8 03  1y.`...&........
 * 00000080: a4 81 00 00 8b 63 00 00 14 00 00 00 c8 20 00 00  .....c....... ..
 * 00000090: 66 50 25 b1 1c e8 fb 16 fa db 7d ae bf 77 cb 54  fP%.......}..w.T
 * 000000a0: a2 ae 39 a1 06 00 52 45 41 44 4d 45 00 00 00 00  ..9...README....
 *
 * # 3. 将 .dircache/index 缓存的内容打包成 tree 对象写入, 生成 tree 数据
 * git-e83c5163$ ./write-tree 
 * cb8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
 *
 * # 4. 再查看 .dircache/objects 下的对象, 新增了一个刚写入的数据
 * git-e83c5163$ tree .dircache -a
 * .dircache
 * ├── index
 * └── objects
 *     ├── 00
 *     ...
 *     ├── 66
 *     │   └── 5025b11ce8fb16fadb7daebf77cb54a2ae39a1
 *     ...
 *     ├── b0
 *     │   └── 4fb99b9a176ff05e03d5e6e739f0a82b83c56c
 *     ...
 *     ├── cb
 *     │   └── 8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
 *     ...
 *     └── ff
 *
 * 257 directories, 4 files
 *
 * # 5. 使用 cat-file 提取刚才写入的 objects 数据到临时文件
 * git-e83c5163$ ./cat-file cb8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
 * temp_git_file_8zPbaQ: tree
 *
 * # 6. 检查临时文件的内容 (tree 对象)
 * git-e83c5163$ xxd -g 1 temp_git_file_8zPbaQ
 * 00000000: 31 30 30 36 34 34 20 4d 61 6b 65 66 69 6c 65 00  100644 Makefile.
 * 00000010: b0 4f b9 9b 9a 17 6f f0 5e 03 d5 e6 e7 39 f0 a8  .O....o.^....9..
 * 00000020: 2b 83 c5 6c 31 30 30 36 34 34 20 52 45 41 44 4d  +..l100644 READM
 * 00000030: 45 00 66 50 25 b1 1c e8 fb 16 fa db 7d ae bf 77  E.fP%.......}..w
 * 00000040: cb 54 a2 ae 39 a1                                .T..9.
 */