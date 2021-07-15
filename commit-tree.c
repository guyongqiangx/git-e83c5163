#include "cache.h"

#include <pwd.h>
#include <time.h>

/* 1 BLOCKING = 16384 Bytes (16KB) */
#define BLOCKING (1ul << 14)
#define ORIG_OFFSET (40)

/*
 * Leave space at the beginning to insert the tag
 * once we know how big things are.
 *
 * FIXME! Share the code with "write-tree.c"
 */
/* 分配 16KB 的 buffer, 并将 sizep 设置为 40 bytes */
static void init_buffer(char **bufp, unsigned int *sizep)
{
	/* 分配 16KB 的 buffer, 并将前 40 bytes 清 0 */
	char *buf = malloc(BLOCKING);
	memset(buf, 0, ORIG_OFFSET);
	/* sizep 大小为 40 bytes */
	*sizep = ORIG_OFFSET;
	*bufp = buf;
}

/* 将 fmt 后变长参数的内容按 fmt 格式转换成字符串, 添加到 bufp 缓冲区 sizep 开始的位置 */
static void add_buffer(char **bufp, unsigned int *sizep, const char *fmt, ...)
{
	char one_line[2048];
	va_list args;
	int len;
	unsigned long alloc, size, newsize;
	char *buf;

	/* 将 fmt 后面的变长参数根据 fmt 指定的格式输出到 one_line 中  */
	va_start(args, fmt);
	len = vsnprintf(one_line, sizeof(one_line), fmt, args);
	va_end(args);
	size = *sizep;
	/* 重新根据实际情况计算长度 */
	newsize = size + len;
	/* 将长度对齐到 32KB 边界, 即按 32KB 粒度分配 */
	alloc = (size + 32767) & ~32767;
	buf = *bufp;
	if (newsize > alloc) {
		alloc = (newsize + 32767) & ~32767;
		/* 在原来的位置重新调整分配内存 */
		buf = realloc(buf, alloc);
		*bufp = buf;
	}
	*sizep = newsize;
	/* 将格式化得到的字符串存放到缓冲区 size (40) 指定的开始地方 */
	memcpy(buf + size, one_line, len);
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

/* 往 bufp 中写入 tag 和附加数据长度 */
static void finish_buffer(char *tag, char **bufp, unsigned int *sizep)
{
	int taglen;
	int offset;
	char *buf = *bufp;
	unsigned int size = *sizep;

	/* 将整个缓冲区填充的长度 size - ORIG_OFFSET, 存放到 ORIG_OFFSET (40) 位置, 返回填充所在位置 offset */
	offset = prepend_integer(buf, size - ORIG_OFFSET, ORIG_OFFSET);
	/* 将 tag 存放到长度字段前面 */
	taglen = strlen(tag);
	offset -= taglen;
	buf += offset;
	size -= offset;
	memcpy(buf, tag, taglen);

	/* 填充后的格式:
	 *                    tag  length  ORIG_OFFSET(40)
	 *                    |       |    |
	 * buffer[] = "_______commiter54321\0________"
	 *             |      |
	 * 返回:       bufp   sizep
	 */

	*bufp = buf;
	*sizep = size;
}

/* 移除字符串中的 '\n', '<'和'>'等特殊字符 */
static void remove_special(char *p)
{
	char c;
	char *dst = p;

	for (;;) {
		c = *p;
		p++;
		switch(c) {
		case '\n': case '<': case '>':
			continue;
		}
		*dst++ = c;
		if (!c)
			break;
	}
}

/*
 * Having more than two parents may be strange, but hey, there's
 * no conceptual reason why the file format couldn't accept multi-way
 * merges. It might be the "union" of several packages, for example.
 *
 * I don't really expect that to happen, but this is here to make
 * it clear that _conceptually_ it's ok..
 */
#define MAXPARENT (16)

int main(int argc, char **argv)
{
	int i, len;
	int parents = 0;
	unsigned char tree_sha1[20];
	unsigned char parent_sha1[MAXPARENT][20];
	char *gecos, *realgecos;
	char *email, realemail[1000];
	char *date, *realdate;
	char comment[1000];
	struct passwd *pw;
	time_t now;
	char *buffer;
	unsigned int size;

	/* 将 argv[1]中的 sha1 字符串转换 sha1 值 */
	if (argc < 2 || get_sha1_hex(argv[1], tree_sha1) < 0)
		usage("commit-tree <sha1> [-p <sha1>]* < changelog");

	for (i = 2; i < argc; i += 2) {
		char *a, *b;
		a = argv[i]; b = argv[i+1];
		/* 提取 -p 选项后的 sha1 字符串, 并将其转换16进制 sha1 值, 存放到 parent_sha1[parents] 中 */
		if (!b || strcmp(a, "-p") || get_sha1_hex(b, parent_sha1[parents]))
			usage("commit-tree <sha1> [-p <sha1>]* < changelog");
		parents++;
	}

	/* 没有指定 -p 参数, 第一次提交 */
	if (!parents)
		fprintf(stderr, "Committing initial tree %s\n", argv[1]);
	/* getpwuid: 返回 passwd 文件中 uid 对应条目的数据, 一个指向 struct passwd 的指针 */
	/* 根据当前用户的 uid 获取用户数据, 构造提交信息: 1. 用户名; 2. 邮件地址; 3. 时间 */
	pw = getpwuid(getuid());
	if (!pw)
		usage("You don't exist. Go away!");
	/* pw_gecos: 用户信息(user information) */
	realgecos = pw->pw_gecos;
	/* pw_name: 用户名(username) */
	len = strlen(pw->pw_name);
	/* 根据用户名和主机名构造用户的邮件信息 user@host */
	memcpy(realemail, pw->pw_name, len);
	realemail[len] = '@';
	gethostname(realemail+len+1, sizeof(realemail)-len-1);
	/* 提取当前时间戳 */
	time(&now);
	realdate = ctime(&now);

	/* 从环境变量提取信息: 1. 用户名; 2. 邮件地址; 3. 时间 */
	gecos = getenv("COMMITTER_NAME") ? : realgecos;
	email = getenv("COMMITTER_EMAIL") ? : realemail;
	date = getenv("COMMITTER_DATE") ? : realdate;

	/* 移除特殊字符 */
	remove_special(gecos); remove_special(realgecos);
	remove_special(email); remove_special(realemail);
	remove_special(date); remove_special(realdate);

	/* 分配 16KB 的 buffer, 并将 size 设置为 40 bytes */
	init_buffer(&buffer, &size);
	/* 将 tree_sha1 值转换成字符串, 以 "tree d486ca60...e83a\n" 的格式, 存放到 buffer 缓冲区 size 开始的位置, 保存后自动调整 size */
	add_buffer(&buffer, &size, "tree %s\n", sha1_to_hex(tree_sha1));

	/*
	 * NOTE! This ordering means that the same exact tree merged with a
	 * different order of parents will be a _different_ changeset even
	 * if everything else stays the same.
	 */
	/* 逐个将 parent_sha1[i] 值转换成字符串, 以 "parent d486ca60...e83a\n" 的格式, 存放到 buffer 缓冲区 size 开始的位置, 保存后自动调整 size */
	for (i = 0; i < parents; i++)
		add_buffer(&buffer, &size, "parent %s\n", sha1_to_hex(parent_sha1[i]));

	/* Person/date information */
	/* 将 author 信息转换成字符串, 以 "author name <email> date\n" 的格式, 存放到 buffer 缓冲区 size 开始的位置, 保存后自动调整 size */
	add_buffer(&buffer, &size, "author %s <%s> %s\n", gecos, email, date);
	/* 将 committer 信息转换成字符串, 以 "committer name <email> date\n\n" 的格式, 存放到 buffer 缓冲区 size 开始的位置, 保存后自动调整 size */
	add_buffer(&buffer, &size, "committer %s <%s> %s\n\n", realgecos, realemail, realdate);

	/* And add the comment */
	/* 将从标准输入获取的 comment 信息存放到 buffer 缓冲区 size 开始的位置, 保存后自动调整 size */
	while (fgets(comment, sizeof(comment), stdin) != NULL)
		add_buffer(&buffer, &size, "%s", comment);

	/* 将 "commit " 和附加数据长度写入到 buffer 中, 从 "commit " 开始到结束实际上是一个 commit 对象 */
	finish_buffer("commit ", &buffer, &size);

	/* 将 commit 对象数据写入到文件中 */
	write_sha1_file(buffer, size);
	return 0;
}

/* #
 * # commit-tree 使用示例
 * #
 *
 * # 1. 在 write-tree 示例中写入过一个 tree 对象(包含 Makefile 和 README)
 * git-e83c5163$ ./write-tree
 * cb8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
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
 * # 2. 使用 commit-tree 提交写入的对象
 * git-e83c5163$ echo "First Commit!" | ./commit-tree cb8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
 * Committing initial tree cb8b8e042b2abdf1070f9f60d83f3fb9cbe204ce
 * 3824d701efdf721efebf5ef04d44817469d7cef4
 *
 * # 3. 使用 cat-file 解析 commit-tree 写入的对象到临时文件中
 * git-e83c5163$ ./cat-file 3824d701efdf721efebf5ef04d44817469d7cef4
 * temp_git_file_QQADM5: commit
 *
 * # 4. 显示 commit-tree 对象的内容
 * git-e83c5163$ xxd -g 1 temp_git_file_QQADM5
 * 00000000: 74 72 65 65 20 63 62 38 62 38 65 30 34 32 62 32  tree cb8b8e042b2
 * 00000010: 61 62 64 66 31 30 37 30 66 39 66 36 30 64 38 33  abdf1070f9f60d83
 * 00000020: 66 33 66 62 39 63 62 65 32 30 34 63 65 0a 61 75  f3fb9cbe204ce.au
 * 00000030: 74 68 6f 72 20 52 6f 63 6b 79 20 47 75 2c 52 6f  thor Rocky Gu,Ro
 * 00000040: 63 6b 79 20 47 75 2c 75 2c 30 30 39 33 35 37 33  cky Gu,u,0093573
 * 00000050: 39 20 3c 72 67 39 33 35 37 33 39 40 73 74 62 73  9 <rg935739@stbs
 * 00000060: 7a 78 2d 62 6c 64 2d 35 3e 20 54 68 75 20 4a 75  zx-bld-5> Thu Ju
 * 00000070: 6c 20 31 35 20 31 34 3a 33 33 3a 30 37 20 32 30  l 15 14:33:07 20
 * 00000080: 32 31 0a 63 6f 6d 6d 69 74 74 65 72 20 52 6f 63  21.committer Roc
 * 00000090: 6b 79 20 47 75 2c 52 6f 63 6b 79 20 47 75 2c 75  ky Gu,Rocky Gu,u
 * 000000a0: 2c 30 30 39 33 35 37 33 39 20 3c 72 67 39 33 35  ,00935739 <rg935
 * 000000b0: 37 33 39 40 73 74 62 73 7a 78 2d 62 6c 64 2d 35  739@stbszx-bld-5
 * 000000c0: 3e 20 54 68 75 20 4a 75 6c 20 31 35 20 31 34 3a  > Thu Jul 15 14:
 * 000000d0: 33 33 3a 30 37 20 32 30 32 31 0a 0a 46 69 72 73  33:07 2021..Firs
 * 000000e0: 74 20 43 6f 6d 6d 69 74 21 0a                    t Commit!.
 */
