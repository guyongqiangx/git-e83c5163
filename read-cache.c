#include "cache.h"

const char *sha1_file_directory = NULL;
struct cache_entry **active_cache = NULL;
unsigned int active_nr = 0, active_alloc = 0;

void usage(const char *err)
{
	fprintf(stderr, "read-tree: %s\n", err);
	exit(1);
}

/*
 * 将字符c[0-9a-fA-F]转换成对应的16进制数值
 */
static unsigned hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return ~0;
}

/*
 * 将 sha1 字符串转换成相应的 sha1 值
 * [hex]"91450428" --> [sha1]0x91,0x45,0x04,0x28
 */
int get_sha1_hex(char *hex, unsigned char *sha1)
{
	int i;
	for (i = 0; i < 20; i++) {
		unsigned int val = (hexval(hex[0]) << 4) | hexval(hex[1]);
		if (val & ~0xff)
			return -1;
		*sha1++ = val;
		hex += 2;
	}
	return 0;
}

/*
 * 将 sha1 值转换成相应的 sha1 字符串
 * [sha1]0x91,0x45,0x04,0x28 --> "91450428"
 */
char * sha1_to_hex(unsigned char *sha1)
{
	static char buffer[50];
	static const char hex[] = "0123456789abcdef";
	char *buf = buffer;
	int i;

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	return buffer;
}

/*
 * NOTE! This returns a statically allocated buffer, so you have to be
 * careful about using it. Do a "strdup()" if you need to save the
 * filename.
 */
/*
 * 返回 sha1 值对应的文件名
 *   如: 914504285ab1fc2a7fca88dbf15f2b48a20b502d
 * 返回: ".dircache/objects/91/4504285ab1fc2a7fca88dbf15f2b48a20b502d"
 */
char *sha1_file_name(unsigned char *sha1)
{
	int i;
	static char *name, *base;

	if (!base) {
		/*
		 * char *sha1_file_directory = getenv("SHA1_FILE_DIRECTORY") ? : ".dircache/objects";
		 * base = ".dircache/objects/__/________...________"
		 *                           |
		 *                           name
		 */
		char *sha1_file_directory = getenv(DB_ENVIRONMENT) ? : DEFAULT_DB_ENVIRONMENT;
		int len = strlen(sha1_file_directory);
		base = malloc(len + 60);
		memcpy(base, sha1_file_directory, len);
		memset(base+len, 0, 60);
		base[len] = '/';
		base[len+3] = '/';
		name = base + len + 1;
	}
	for (i = 0; i < 20; i++) {
		static char hex[] = "0123456789abcdef";
		unsigned int val = sha1[i];
		char *pos = name + i*2 + (i > 0); /* (i>0)用于调整文件名中的'/'字符带来的偏差 */
		*pos++ = hex[val >> 4];
		*pos = hex[val & 0xf];
	}
	return base;
}

/*
 * 返回 sha1 值对应的文件内容(解压缩后返回)
 */
void * read_sha1_file(unsigned char *sha1, char *type, unsigned long *size)
{
	z_stream stream;
	char buffer[8192];
	struct stat st;
	int i, fd, ret, bytes;
	void *map, *buf;
	/* 将 sha1 值转换成文件名 */
	char *filename = sha1_file_name(sha1);

	/* 打开文件 */
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		return NULL;
	}
	/* 获取文件大小 */
	if (fstat(fd, &st) < 0) {
		close(fd);
		return NULL;
	}
	/* 将文件映射到内存，方便访问 */
	map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (-1 == (int)(long)map)
		return NULL;

	/* Get the data stream */
	memset(&stream, 0, sizeof(stream));
	stream.next_in = map;
	stream.avail_in = st.st_size;
	stream.next_out = buffer;
	stream.avail_out = sizeof(buffer);

	inflateInit(&stream);
	ret = inflate(&stream, 0);
	/* 解析 buffer 中的数据到 type 和 size 中 */
	if (sscanf(buffer, "%10s %lu", type, size) != 2)
		return NULL;
	bytes = strlen(buffer) + 1;
	buf = malloc(*size);
	if (!buf)
		return NULL;

	memcpy(buf, buffer + bytes, stream.total_out - bytes);
	bytes = stream.total_out - bytes;
	if (bytes < *size && ret == Z_OK) {
		stream.next_out = buf + bytes;
		stream.avail_out = *size - bytes;
		while (inflate(&stream, Z_FINISH) == Z_OK)
			/* nothing */;
	}
	inflateEnd(&stream);
	return buf;
}

/*
 * 将 buf 数据写入到文件中
 * 1. 压缩 buf 数据;
 * 2. 计算压缩数据的 sha1 值;
 * 3. 将压缩后数据写入 sha1 值对应的文件中;
 */
int write_sha1_file(char *buf, unsigned len)
{
	int size;
	char *compressed;
	z_stream stream;
	unsigned char sha1[20];
	SHA_CTX c;

	/* 压缩传入的 buf 数据 */
	/* Set it up */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, Z_BEST_COMPRESSION);
	size = deflateBound(&stream, len);
	compressed = malloc(size);

	/* Compress it */
	stream.next_in = buf;
	stream.avail_in = len;
	stream.next_out = compressed;
	stream.avail_out = size;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);
	size = stream.total_out;

	/* 计算压缩后数据的SHA1哈希值 */
	/* Sha1.. */
	SHA1_Init(&c);
	SHA1_Update(&c, compressed, size);
	SHA1_Final(sha1, &c);

	/* 将压缩后的数据写入到 sha1 值对应的文件中 */
	if (write_sha1_buffer(sha1, compressed, size) < 0)
		return -1;
	printf("%s\n", sha1_to_hex(sha1));
	return 0;
}

/*
 * 将 buf 中的数据写入到 sha1 值对应的文件中
 */
int write_sha1_buffer(unsigned char *sha1, void *buf, unsigned int size)
{
	/* 将 sha1 值转换成文件名 filename */
	char *filename = sha1_file_name(sha1);
	int i, fd;

	/* 打开文件, 写入 buf 中的数据 */
	fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0)
		return (errno == EEXIST) ? 0 : -1;
	write(fd, buf, size);
	close(fd);
	return 0;
}

static int error(const char * string)
{
	fprintf(stderr, "error: %s\n", string);
	return -1;
}

/*
 * 校验 hdr 部分的 sha1 数据是否正确
 * 1. 计算 hdr 数据的哈希值
 * 2. 同 hdr 的 sha1 成员值比较
 */
static int verify_hdr(struct cache_header *hdr, unsigned long size)
{
	SHA_CTX c;
	unsigned char sha1[20];

	if (hdr->signature != CACHE_SIGNATURE)
		return error("bad signature");
	if (hdr->version != 1)
		return error("bad version");
	SHA1_Init(&c);
	/* 计算 hdr 部分除 sha1 外的哈希值 */
	SHA1_Update(&c, hdr, offsetof(struct cache_header, sha1));
	/* 累积 hdr 以后到数据结束的哈希值, 中间跳过 hdr 的 sha1 部分 */
	SHA1_Update(&c, hdr+1, size - sizeof(*hdr));
	SHA1_Final(sha1, &c);
	if (memcmp(sha1, hdr->sha1, 20))
		return error("bad header sha1");
	return 0;
}

/* 读取索引文件".dircache/index", 建立缓存, 返回条目数 */
int read_cache(void)
{
	int fd, i;
	struct stat st;
	unsigned long size, offset;
	void *map;
	struct cache_header *hdr;

	errno = EBUSY;
	if (active_cache)
		return error("more than one cachefile");
	errno = ENOENT;
	sha1_file_directory = getenv(DB_ENVIRONMENT);
	if (!sha1_file_directory)
		sha1_file_directory = DEFAULT_DB_ENVIRONMENT;
	if (access(sha1_file_directory, X_OK) < 0)
		return error("no access to SHA1 file directory");
	/* 打开文件 ".dircache/index" */
	fd = open(".dircache/index", O_RDONLY);
	if (fd < 0)
		return (errno == ENOENT) ? 0 : error("open failed");

	/* 映射文件到内存 */
	map = (void *)-1;
	if (!fstat(fd, &st)) {
		map = NULL;
		size = st.st_size;
		errno = EINVAL;
		if (size > sizeof(struct cache_header))
			map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	}
	close(fd);
	if (-1 == (int)(long)map)
		return error("mmap failed");

	/* 检查映射数据的 hdr */
	hdr = map;
	if (verify_hdr(hdr, size) < 0)
		goto unmap;

	/* 计算 hdr 中的条目数 */
	active_nr = hdr->entries;
	active_alloc = alloc_nr(active_nr);
	active_cache = calloc(active_alloc, sizeof(struct cache_entry *));

	offset = sizeof(*hdr);
	for (i = 0; i < hdr->entries; i++) {
		struct cache_entry *ce = map + offset;
		offset = offset + ce_size(ce);
		active_cache[i] = ce;
	}
	return active_nr;

unmap:
	munmap(map, size);
	errno = EINVAL;
	return error("verify header failed");
}

