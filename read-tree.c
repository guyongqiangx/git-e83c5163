#include "cache.h"

/*
 * 提取 sha1 值对应 tree 对象, 并显示每一个文件的 mode path sha1 数据
 */
static int unpack(unsigned char *sha1)
{
	void *buffer;
	unsigned long size;
	char type[20];

	/* 获取 sha1 值对应的文件内容(解压缩) */
	buffer = read_sha1_file(sha1, type, &size);
	if (!buffer)
		usage("unable to read sha1 file");
	/* 检查 sha1 文件数据的类型是否为 tree */
	if (strcmp(type, "tree"))
		usage("expected a 'tree' node");
	while (size) {
		int len = strlen(buffer)+1;
		unsigned char *sha1 = buffer + len;
		/* 查找空格' '字符 */
		char *path = strchr(buffer, ' ')+1;
		unsigned int mode;
		/* 使用 scanf + %o 提取文件的 mode 数据 */
		if (size < len + 20 || sscanf(buffer, "%o", &mode) != 1)
			usage("corrupt 'tree' file");
		buffer = sha1 + 20;
		size -= len + 20;
		/* 打印展示 tree 对象中每一条数据的 mode, path, sha1 */
		printf("%o %s (%s)\n", mode, path, sha1_to_hex(sha1));
	}
	return 0;
}

int main(int argc, char **argv)
{
	int fd;
	unsigned char sha1[20];

	if (argc != 2)
		usage("read-tree <key>");
	/* 将 argv[1] 的 sha1 字符串转换成 sha1 值 */
	if (get_sha1_hex(argv[1], sha1) < 0)
		usage("read-tree <key>");
	/* sha1_file_directory = getenv("SHA1_FILE_DIRECTORY") */
	sha1_file_directory = getenv(DB_ENVIRONMENT);
	if (!sha1_file_directory)
		sha1_file_directory = DEFAULT_DB_ENVIRONMENT;
	/* 解包并打印 sha1 值对应文件的 mode path sha1 数据 */
	if (unpack(sha1) < 0)
		usage("unpack failed");
	return 0;
}

/* #
 * # read-tree 使用示例
 * #
 *
 * # 1. 在 write-tree 示例中写入过一个 tree 对象(包含 Makefile 和 README)
 * git-e83c5163$ ./write-tree
 * cb8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
 *
 * # 2. 使用 cat-file 提取 tree 对象到临时文件
 * git-e83c5163$ ./cat-file cb8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
 * temp_git_file_13xlrH: tree
 *
 * # 3. 查看 tree 对象内容
 * git-e83c5163$ xxd -g 1 temp_git_file_13xlrH
 * 00000000: 31 30 30 36 34 34 20 4d 61 6b 65 66 69 6c 65 00  100644 Makefile.
 * 00000010: b0 4f b9 9b 9a 17 6f f0 5e 03 d5 e6 e7 39 f0 a8  .O....o.^....9..
 * 00000020: 2b 83 c5 6c 31 30 30 36 34 34 20 52 45 41 44 4d  +..l100644 READM
 * 00000030: 45 00 66 50 25 b1 1c e8 fb 16 fa db 7d ae bf 77  E.fP%.......}..w
 * 00000040: cb 54 a2 ae 39 a1                                .T..9.
 *
 * # 4. 使用 read-tree 读取 tree 对象
 * git-e83c5163$ ./read-tree cb8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
 * 100644 Makefile (b04fb99b9a176ff05e03d5e6e739f0a82b83c56c)
 * 100644 README (665025b11ce8fb16fadb7daebf77cb54a2ae39a1)
 *
 * # 5. 使用 read-tree 尝试读取一个 blob 对象 (Makefile 的 blob 对象)
 * git-e83c5163$ ./read-tree b04fb99b9a176ff05e03d5e6e739f0a82b83c56c
 * read-tree: expected a 'tree' node
 */