#include "cache.h"

/*
 * 比较字符串 name1 和 name2
 */
static int cache_name_compare(const char *name1, int len1, const char *name2, int len2)
{
	int len = len1 < len2 ? len1 : len2;
	int cmp;

	cmp = memcmp(name1, name2, len);
	if (cmp)
		return cmp;
	if (len1 < len2)
		return -1;
	if (len1 > len2)
		return 1;
	return 0;
}

static int cache_name_pos(const char *name, int namelen)
{
	int first, last;

	first = 0;
	last = active_nr;
	while (last > first) {
		int next = (last + first) >> 1;
		struct cache_entry *ce = active_cache[next];
		int cmp = cache_name_compare(name, namelen, ce->name, ce->namelen);
		if (!cmp)
			return -next-1;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	return first;
}

static int remove_file_from_cache(char *path)
{
	int pos = cache_name_pos(path, strlen(path));
	if (pos < 0) {
		pos = -pos-1;
		active_nr--;
		if (pos < active_nr)
			memmove(active_cache + pos, active_cache + pos + 1, (active_nr - pos - 1) * sizeof(struct cache_entry *));
	}
}

static int add_cache_entry(struct cache_entry *ce)
{
	int pos;

	pos = cache_name_pos(ce->name, ce->namelen);

	/* existing match? Just replace it */
	if (pos < 0) {
		active_cache[-pos-1] = ce;
		return 0;
	}

	/* Make sure the array is big enough .. */
	if (active_nr == active_alloc) {
		active_alloc = alloc_nr(active_alloc);
		active_cache = realloc(active_cache, active_alloc * sizeof(struct cache_entry *));
	}

	/* Add it in.. */
	active_nr++;
	if (active_nr > pos)
		memmove(active_cache + pos + 1, active_cache + pos, (active_nr - pos - 1) * sizeof(ce));
	active_cache[pos] = ce;
	return 0;
}

/*
 * 将 fd 指向的文件写入到 blob 数据中
 * 1. 压缩数据("blob 987654" + data)
 * 2. 计算压缩数据 sha1 值
 * 3. 将压缩后数据写入到 sha1 值对应的文件中
 */
static int index_fd(const char *path, int namelen, struct cache_entry *ce, int fd, struct stat *st)
{
	z_stream stream;
	int max_out_bytes = namelen + st->st_size + 200;
	void *out = malloc(max_out_bytes);
	void *metadata = malloc(namelen + 200);
	/* 将 fd 指定的文件映射到内存 */
	void *in = mmap(NULL, st->st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	SHA_CTX c;

	close(fd);
	if (!out || (int)(long)in == -1)
		return -1;

	/* 压缩文件数据 */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, Z_BEST_COMPRESSION);

	/*
	 * ASCII size + nul byte
	 */	
	stream.next_in = metadata;
	stream.avail_in = 1+sprintf(metadata, "blob %lu", (unsigned long) st->st_size);
	stream.next_out = out;
	stream.avail_out = max_out_bytes;
	while (deflate(&stream, 0) == Z_OK)
		/* nothing */;

	/*
	 * File content
	 */
	stream.next_in = in;
	stream.avail_in = st->st_size;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/*nothing */;

	deflateEnd(&stream);
	
	/* 计算压缩数据的SHA1哈希值 */
	SHA1_Init(&c);
	SHA1_Update(&c, out, stream.total_out);
	SHA1_Final(ce->sha1, &c);

	/* 将压缩数据写入到 sha1 值对应的文件中 */
	return write_sha1_buffer(ce->sha1, out, stream.total_out);
}

/*
 * 将 path 指定的文件内容写入到 blob 数据中, 并将文件信息存放到 cache entry 中
 * 1. 获取 path 指定的文件信息
 * 2. 将文件内容写入到 blob 数据中
 *    1). 添加 "blob 93276" 头部 (93276 为假设的文件长度)
 *    2). 压缩头部和文件数据
 *    4). 计算压缩数据的 sha1 值
 *    3). 将压缩数据写入 sha1 值对应的文件中
 * 3. 将文件信息保存到 cache entry 中
 */
static int add_file_to_cache(char *path)
{
	int size, namelen;
	struct cache_entry *ce;
	struct stat st;
	int fd;

	/* 打开 path 指定文件, 用于后续提取文件信息和内容 */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return remove_file_from_cache(path);
		return -1;
	}

	/* 获取文件信息 */
	if (fstat(fd, &st) < 0) {
		close(fd);
		return -1;
	}

	/* 分配 cache entry 条目, 用于存放文件信息 */
	namelen = strlen(path);
	size = cache_entry_size(namelen);
	ce = malloc(size);
	memset(ce, 0, size);
	memcpy(ce->name, path, namelen);
	ce->ctime.sec = st.st_ctime;
	ce->ctime.nsec = st.st_ctim.tv_nsec;
	ce->mtime.sec = st.st_mtime;
	ce->mtime.nsec = st.st_mtim.tv_nsec;
	ce->st_dev = st.st_dev;
	ce->st_ino = st.st_ino;
	ce->st_mode = st.st_mode;
	ce->st_uid = st.st_uid;
	ce->st_gid = st.st_gid;
	ce->st_size = st.st_size;
	ce->namelen = namelen;

	/* 将文件内容写入到 blob 数据中 */
	if (index_fd(path, namelen, ce, fd, &st) < 0)
		return -1;

	/* 将 cache entry 条目添加到缓存列表中 */
	return add_cache_entry(ce);
}

/*
 * 将 cache entry 中的数据写入到 newfd 指定的文件中
 */
static int write_cache(int newfd, struct cache_entry **cache, int entries)
{
	SHA_CTX c;
	struct cache_header hdr;
	int i;

	hdr.signature = CACHE_SIGNATURE;
	hdr.version = 1;
	hdr.entries = entries;

	/* 计算 cache entry 的哈希值 */
	SHA1_Init(&c);
	SHA1_Update(&c, &hdr, offsetof(struct cache_header, sha1));
	/* 遍历每个 cache entry 条目, 累积计算每个条目的 sha1 */
	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = cache[i];
		int size = ce_size(ce);
		SHA1_Update(&c, ce, size);
	}
	/* 最终得到的 sha1 值写入到 hdr.sha1 中 */
	SHA1_Final(hdr.sha1, &c);

	/* 保存 hdr 数据到 fd */
	if (write(newfd, &hdr, sizeof(hdr)) != sizeof(hdr))
		return -1;

	/* 逐条保存 cache entry 条目数据到文件 newfd */
	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = cache[i];
		int size = ce_size(ce);
		if (write(newfd, ce, size) != size)
			return -1;
	}
	return 0;
}		

/*
 * We fundamentally don't like some paths: we don't want
 * dot or dot-dot anywhere, and in fact, we don't even want
 * any other dot-files (.dircache or anything else). They
 * are hidden, for chist sake.
 *
 * Also, we don't want double slashes or slashes at the
 * end that can make pathnames ambiguous. 
 */
/*
 * 检查 path 字符串中是否包含'.'和'\'字符
 */
static int verify_path(char *path)
{
	char c;

	goto inside;
	for (;;) {
		if (!c)
			return 1;
		if (c == '/') {
inside:
			c = *path++;
			if (c != '/' && c != '.' && c != '\0')
				continue;
			return 0;
		}
		c = *path++;
	}
}

/*
 * 添加新文件到暂存区(cache)中, 现在叫 staging
 * 1. 文件内容写入到 blob 数据中
 * 2. 文件信息添加到 ".dircache/index" 中
 */
int main(int argc, char **argv)
{
	int i, newfd, entries;

	/* 读取索引文件".dircache/index"到内存, 建立缓存 */
	entries = read_cache();
	if (entries < 0) {
		perror("cache corrupted");
		return -1;
	}

	/* 创建lock文件: ".dircache/index.lock" */
	newfd = open(".dircache/index.lock", O_RDWR | O_CREAT | O_EXCL, 0600);
	if (newfd < 0) {
		perror("unable to create new cachefile");
		return -1;
	}
	for (i = 1 ; i < argc; i++) {
		char *path = argv[i];
		/* 检查 path 字符串中是否包含'.'和'\'字符 */
		if (!verify_path(path)) {
			fprintf(stderr, "Ignoring path %s\n", argv[i]);
			continue;
		}
		/*
		 * 将 path 指定的文件数据写入 blob 中, 文件信息写入到 cache entry 中
		 */
		if (add_file_to_cache(path)) {
			fprintf(stderr, "Unable to add %s to database\n", path);
			goto out;
		}
	}
	/* 将更新后的 cache entry 写入到 ".dircache/index.lock" 文件, 并命名回 ".dircache/index" */
	if (!write_cache(newfd, active_cache, active_nr) && !rename(".dircache/index.lock", ".dircache/index"))
		return 0;
out:
	unlink(".dircache/index.lock");
}

/* #
 * # update-cache 使用示例
 * #
 *
 * # 1. 使用 update-cache 将 Makefile 添加到暂存区
 * git-e83c5163$ ./update-cache Makefile
 *
 * # 2. 查看新增的 object 数据 (第一次添加文件, 原来的内容为空)
 * git-e83c5163$ tree .dircache/ -a
 * .dircache/
 * ├── index
 * └── objects
 *     ├── 00
 *     ...
 *     ├── b0
 *     │   └── 4fb99b9a176ff05e03d5e6e739f0a82b83c56c
 *     ├── b1
 *     ...
 *     └── ff
 *
 * 257 directories, 2 files
 *
 * # 3. 查看索引文件的内容 (Makefile 文件信息已经添加到该文件)
 * git-e83c5163$ xxd -g 1 .dircache/index
 * 00000000: 43 52 49 44 01 00 00 00 01 00 00 00 1c ee 04 41  CRID...........A
 * 00000010: 4a 8b 00 b2 05 55 e7 6b 2c f6 d6 40 2f c4 2d 21  J....U.k,..@/.-!
 * 00000020: 10 7b ed 60 b6 28 26 27 10 7b ed 60 b6 28 26 27  .{.`.(&'.{.`.(&'
 * 00000030: 03 fc 00 00 86 83 e8 03 a4 81 00 00 8b 63 00 00  .............c..
 * 00000040: 14 00 00 00 ca 03 00 00 b0 4f b9 9b 9a 17 6f f0  .........O....o.
 * 00000050: 5e 03 d5 e6 e7 39 f0 a8 2b 83 c5 6c 08 00 4d 61  ^....9..+..l..Ma
 * 00000060: 6b 65 66 69 6c 65 00 00                          kefile..
 *
 * # 4. 文件存储的数据时以其 sha1 值命名, 这里使用 sha1sum 工具验证下
 * git-e83c5163$ sha1sum .dircache/objects/b0/4fb99b9a176ff05e03d5e6e739f0a82b83c56c
 * b04fb99b9a176ff05e03d5e6e739f0a82b83c56c  .dircache/objects/b0/4fb99b9a176ff05e03d5e6e739f0a82b83c56c
 *
 * # 5. 使用 update-cache 再新增一个文件
 * git-e83c5163$ ./update-cache README
 *
 * # 6. 查看新增的 object 数据 (第二次添加文件, 原来已经有一个文件)
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
 * # 7. 查看索引文件内容 (在 Makefile 文件后增加了 README 文件的信息)
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
 */