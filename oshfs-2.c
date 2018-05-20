#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <unistd.h>
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define OVERHEAD sizeof(block_header)
#define HDRP(bp) ((char *)(bp) - sizeof(block_header))
#define GET_SIZE(p)  ((block_header *)(p))->size
#define GET_ALLOC(p) ((block_header *)(p))->allocated
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define CHUNK_SIZE (1 << 14)
#define CHUNK_ALIGN(size) (((size)+(CHUNK_SIZE-1)) & ~(CHUNK_SIZE-1))

struct filenode {
    char *filename;
    void *content;
    struct stat *st;
    struct filenode *next;
};

typedef struct {
  size_t size;
  char   allocated;
}block_header;

void *first_bp;

int mm_init() {
  sbrk(sizeof(block_header));
  first_bp = sbrk(0);
  GET_SIZE(HDRP(first_bp)) = 0;
  GET_ALLOC(HDRP(first_bp)) = 1;
  return 0;
}

void extend(size_t new_size) {
  size_t chunk_size = CHUNK_ALIGN(new_size);
  void *bp = sbrk(chunk_size);
  GET_SIZE(HDRP(bp)) = chunk_size;
  GET_ALLOC(HDRP(bp)) = 0;
  GET_SIZE(HDRP(NEXT_BLKP(bp))) = 0;
  GET_ALLOC(HDRP(NEXT_BLKP(bp))) = 1;
}

void set_allocated(void *bp, size_t size) {
  size_t extra_size = GET_SIZE(HDRP(bp)) - size;
  if (extra_size > ALIGN(1 + OVERHEAD)) {
    GET_SIZE(HDRP(bp)) = size;
    GET_SIZE(HDRP(NEXT_BLKP(bp))) = extra_size;
    GET_ALLOC(HDRP(NEXT_BLKP(bp))) = 0;
  }
  GET_ALLOC(HDRP(bp)) = 1;
}

void *mm_malloc(size_t size) {
  int new_size = ALIGN(size + OVERHEAD);
  void *bp = first_bp;
  while (GET_SIZE(HDRP(bp)) != 0) {
    if (!GET_ALLOC(HDRP(bp))
        && (GET_SIZE(HDRP(bp)) >= new_size)) {
      set_allocated(bp, new_size);
      return bp;
    }
    bp = NEXT_BLKP(bp);
  }
  extend(new_size);
  set_allocated(bp, new_size);
  return bp;
}

void mm_free(void *bp) {
  GET_ALLOC(HDRP(bp)) = 0;
}

void *mm_realloc(void *ptr, size_t size) {
    void *ptr2 = mm_malloc(size);
    int _size = 0;
    if(ptr != NULL)
    {
        _size = GET_SIZE(HDRP(ptr));
        memcpy(ptr2, ptr, _size);
        mm_free(ptr);
    }
    set_allocated(ptr2, size);
    return ptr2;
}

static const size_t size = 4 * 1024 * 1024 * (size_t)1024;
static void *mem[64 * 1024];

static struct filenode *root = NULL;

static struct filenode *get_filenode(const char *name)
{
    struct filenode *node = root;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)
{
    struct filenode *new = (struct filenode *)malloc(sizeof(struct filenode));
    new->filename = (char *)malloc(strlen(filename) + 1);
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->st = (struct stat *)malloc(sizeof(struct stat));
    memcpy(new->st, st, sizeof(struct stat));
    new->next = root;
    new->content = NULL;
    root = new;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
    /*size_t blocknr = sizeof(mem) / sizeof(mem[0]);
    size_t blocksize = size / blocknr;
    // Demo 1
    for(int i = 0; i < blocknr; i++) {
        mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }
    // Demo 2
    mem[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(int i = 0; i < blocknr; i++) {
        mem[i] = (char *)mem[0] + blocksize * i;
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }*/
    mm_init();
    return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    struct filenode *node = get_filenode(path);
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    } else if(node) {
        memcpy(stbuf, node->st, sizeof(struct stat));
    } else {
        ret = -ENOENT;
    }
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct filenode *node = root;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node) {
        filler(buf, node->filename, node->st, 0);
        node = node->next;
    }
    return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    struct stat st;
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    create_filenode(path + 1, &st);
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct filenode *node = get_filenode(path);
    node->st->st_size = offset + size;
    node->content = realloc(node->content, offset + size);
    memcpy(node->content + offset, buf, size);
    return size;
}

static int oshfs_truncate(const char *path, off_t size)
{
    struct filenode *node = get_filenode(path);
    node->st->st_size = size;
    node->content = realloc(node->content, size);
    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct filenode *node = get_filenode(path);
    int ret = size;
    if(offset + size > node->st->st_size)
        ret = node->st->st_size - offset;
    memcpy(buf, node->content + offset, ret);
    return ret;
}

static int oshfs_unlink(const char *path)
{
    // Not Implemented
    struct filenode *p, *node = get_filenode(path);
    p = root;
    if (p != node)
    {
        while(p->next != node)
            p = p->next;
        p->next = node->next;
    }
    else
        root = node->next;
    free(node->st);
    free(node->content);
    free(node);
    return 0;
    return 0;
}

static const struct fuse_operations op = {
    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);
}
