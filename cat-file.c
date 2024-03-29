#include "cache.h"

/*
 * 命令: "cat-file <sha1>"
 * 示例: $ ./cat-file b04fb99b9a176ff05e03d5e6e739f0a82b83c56c
 */
int main(int argc, char **argv)
{
	unsigned char sha1[20];
	char type[20];
	void *buf;
	unsigned long size;
	char template[] = "temp_git_file_XXXXXX";
	int fd;

	/* 将 argv[1] 的16进制 sha1 字符串转换成 sha1 值 */
	if (argc != 2 || get_sha1_hex(argv[1], sha1))
		usage("cat-file: cat-file <sha1>");
	/* 获取 sha1 值对应的文件内容(解压缩后返回) */
	/* NOTE!!! 这里的 buf 是在 read_sha1_file 内部 malloc 的, 使用后没有释放会造成内存泄露 */
	buf = read_sha1_file(sha1, type, &size);
	if (!buf)
		exit(1);
	/* 创建临时文件 */
	fd = mkstemp(template);
	if (fd < 0)
		usage("unable to create tempfile");
	/* 将解压缩后的 sha1 值对应的文件内容写入临时文件 temp_git_file_XXXXXX */
	if (write(fd, buf, size) != size)
		strcpy(type, "bad");
	printf("%s: %s\n", template, type);
}

/* #
 * # cat-file 使用示例
 * #
 *
 * # 0. 命令格式
 * # $ cat-file <sha1>
 *
 * # 1. 添加文件, 并查看已有的 sha1 文件对象
 * git-e83c5163$ ./update-cache Makefile
 * git-e83c5163$ ls .dircache/objects/b0/
 * 4fb99b9a176ff05e03d5e6e739f0a82b83c56c
 *
 * # 2. 使用 cat-file 操作提取 sha1 文件对象 (找到 sha1 文件对象, 提取内容并解压缩到临时文件中)
 * git-e83c5163$ ./cat-file b04fb99b9a176ff05e03d5e6e739f0a82b83c56c
 * temp_git_file_Xq29L3: blob
 *
 * # 3. 查看 cat-file 生成的临时文件的内容
 * git-e83c5163$ head -5 temp_git_file_Xq29L3
 * CFLAGS=-g
 * CC= *
 * PROG=update-cache show-diff init-db write-tree read-tree commit-tree cat- *
 * git-e83c5163$
 */
