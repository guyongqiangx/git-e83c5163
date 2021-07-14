#include "cache.h"

/*
 * 1. 创建目录".dircache/objects/{00,01,02,...,fd,fe,ff}"
 * $ tree .dircache
 * .dircache
 * └── objects
 * 	├── 00
 * 	├── 01
 * 	├── 02
 * 	...
 * 	├── fd
 * 	├── fe
 * 	└── ff
 *
 * 257 directories, 0 files
 */
int main(int argc, char **argv)
{
	char *sha1_dir = getenv(DB_ENVIRONMENT), *path;
	int len, i, fd;

	/*
	 * 创建名为".dircache"的文件夹, 权限模式为0700
	 * 如果".dircache"已经存在，则mkdir会在这里失败并返回
	 *
	 * $ ./init-db
	 * unable to create .dircache: File exists
	 */
	if (mkdir(".dircache", 0700) < 0) {
		perror("unable to create .dircache");
		exit(1);
	}

	/*
	 * If you want to, you can share the DB area with any number of branches.
	 * That has advantages: you can save space by sharing all the SHA1 objects.
	 * On the other hand, it might just make lookup slower and messier. You
	 * be the judge.
	 */
	/* sha1_dir = getenv("SHA1_FILE_DIRECTORY")，判断环境变量"SHA1_FILE_DIRECTORY"指向的文件是否存在，其是否为目录 */
	sha1_dir = getenv(DB_ENVIRONMENT);
	if (sha1_dir) {
		struct stat st;
		if (!stat(sha1_dir, &st) < 0 && S_ISDIR(st.st_mode))
			return;
		fprintf(stderr, "DB_ENVIRONMENT set to bad directory %s: ", sha1_dir);
	}

	/*
	 * The default case is to have a DB per managed directory. 
	 */
	/* sha1_dir=".dircache/objects"，创建该目录 */
	sha1_dir = DEFAULT_DB_ENVIRONMENT;
	fprintf(stderr, "defaulting to private storage area\n");
	len = strlen(sha1_dir);
	if (mkdir(sha1_dir, 0700) < 0) {
		if (errno != EEXIST) {
			perror(sha1_dir);
			exit(1);
		}
	}
	/*
	 * 在".dircache/objects"创建256个子目录，结果如下:
	 * $ tree .dircache
	 * .dircache
	 * └── objects
	 * 	├── 00
	 * 	├── 01
	 * 	├── 02
	 * 	...
	 * 	├── fd
	 * 	├── fe
	 * 	└── ff
	 *
	 * 257 directories, 0 files
	 */
	path = malloc(len + 40);
	memcpy(path, sha1_dir, len);
	for (i = 0; i < 256; i++) {
		sprintf(path+len, "/%02x", i);
		if (mkdir(path, 0700) < 0) {
			if (errno != EEXIST) {
				perror(path);
				exit(1);
			}
		}
	}
	return 0;
}

/* #
 * # init-db 使用示例
 * #
 * git-e83c5163$ ./init-db
 * defaulting to private storage area
 * git-e83c5163$ tree .dircache
 * .dircache
 * └── objects
 *     ├── 00
 *     ├── 01
 *     ...
 *     └── ff
 *
 * 257 directories, 0 files
 * git-e83c5163$ ./init-db
 * unable to create .dircache: File exists
 */
