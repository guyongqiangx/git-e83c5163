#include "cache.h"

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020

/*
 * 比较 cache entry 和 stat 结构体中存放的文件信息
 */
static int match_stat(struct cache_entry *ce, struct stat *st)
{
	unsigned int changed = 0;

	if (ce->mtime.sec  != (unsigned int)st->st_mtim.tv_sec ||
	    ce->mtime.nsec != (unsigned int)st->st_mtim.tv_nsec)
		changed |= MTIME_CHANGED;
	if (ce->ctime.sec  != (unsigned int)st->st_ctim.tv_sec ||
	    ce->ctime.nsec != (unsigned int)st->st_ctim.tv_nsec)
		changed |= CTIME_CHANGED;
	if (ce->st_uid != (unsigned int)st->st_uid ||
	    ce->st_gid != (unsigned int)st->st_gid)
		changed |= OWNER_CHANGED;
	if (ce->st_mode != (unsigned int)st->st_mode)
		changed |= MODE_CHANGED;
	if (ce->st_dev != (unsigned int)st->st_dev ||
	    ce->st_ino != (unsigned int)st->st_ino)
		changed |= INODE_CHANGED;
	if (ce->st_size != (unsigned int)st->st_size)
		changed |= DATA_CHANGED;
	return changed;
}

/*
 * 比较 old_contents 和 cache entry 条目中对应的文件数据
 */
static void show_differences(struct cache_entry *ce, struct stat *cur,
	void *old_contents, unsigned long long old_size)
{
	static char cmd[1000];
	FILE *f;

	/* 生成 diff 命令: "diff -u - filename", 比较标准输入中的内容和 cache entry 条目对应的文件 */
	snprintf(cmd, sizeof(cmd), "diff -u - %s", ce->name);
	/* 打开管道 */
	f = popen(cmd, "w");
	/* 往命令管道中写入数据 old_contents */
	fwrite(old_contents, old_size, 1, f);
	pclose(f);
}

int main(int argc, char **argv)
{
	/* 读取索引文件".dircache/index"到内存, 建立缓存 */
	int entries = read_cache();
	int i;

	if (entries < 0) {
		perror("read_cache");
		exit(1);
	}
	/* 遍历缓存中的条目, 和工作目录下同名文件进行比较 */
	for (i = 0; i < entries; i++) {
		struct stat st;
		struct cache_entry *ce = active_cache[i];
		int n, changed;
		unsigned int mode;
		unsigned long size;
		char type[20];
		void *new;

		/* 提取 cache entry 条目同名的文件信息 */
		if (stat(ce->name, &st) < 0) {
			printf("%s: %s\n", ce->name, strerror(errno));
			continue;
		}
		/* 将提取的文件信息与 cache entry 条目存储的文件信息, */
		changed = match_stat(ce, &st);
		if (!changed) {
			printf("%s: ok\n", ce->name);
			continue;
		}
		printf("%.*s:  ", ce->namelen, ce->name);
		for (n = 0; n < 20; n++)
			printf("%02x", ce->sha1[n]);
		printf("\n");
		/* 获取 cache entry 条目对应的文件内容 */
		new = read_sha1_file(ce->sha1, type, &size);
		/* 将 cache entry 条目对应的文件和暂存区已经添加的内容进行比较 */
		show_differences(ce, &st, new, size);
		free(new);
	}
	return 0;
}

/* #
 * # show-diff 使用示例
 * #
 *
 * # 1. 检查已经添加到 cache 的文件
 * git-e83c5163$ xxd -g 1 .dircache/index
 * 0000000: 43 52 49 44 01 00 00 00 01 00 00 00 f5 12 43 49  CRID..........CI
 * 0000010: d6 cd d6 2e 1a a0 37 1d 89 e7 00 e9 9a 25 4f e9  ......7......%O.
 * 0000020: 54 f5 ee 60 09 28 8e 14 54 f5 ee 60 09 28 8e 14  T..`.(..T..`.(..
 * 0000030: 11 08 00 00 79 00 dc 00 b4 81 00 00 e8 03 00 00  ....y...........
 * 0000040: e8 03 00 00 ca 03 00 00 b0 4f b9 9b 9a 17 6f f0  .........O....o.
 * 0000050: 5e 03 d5 e6 e7 39 f0 a8 2b 83 c5 6c 08 00 4d 61  ^....9..+..l..Ma
 * 0000060: 6b 65 66 69 6c 65 00 00                          kefile..
 *
 * # 2. 使用 show-diff 比较工作区的 Makefile 和暂存区的数据
 * git-e83c5163$ ./show-diff Makefile
 * Makefile:  b04fb99b9a176ff05e03d5e6e739f0a82b83c56c
 * --- -	2021-07-14 23:13:43.918975406 +0800
 * +++ Makefile	2021-07-14 23:13:04.832860681 +0800
 * @@ -1,3 +1,5 @@
 * +.PHONY: all
 * +
 *  CFLAGS=-g
 *  CC=gcc
 *
 */