#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#define FUSE_USE_VERSION 26
#define BLOCK_SIZE 1024 * 8
#define SPACE 4
#define BITMAP_BLOCK sizeof(long long)*8
#define BLK_ARY 64

struct block_array {
    int bid[BLK_ARY];
    struct block_array * next;
};

struct filenode {
    char *filename;
    struct block_array *content;
    struct stat *st;
    struct filenode *next;
};
static const size_t size = SPACE * 1024 * 1024 * (size_t)1024;
static const size_t block_num = size/BLOCK_SIZE;
static const int _bitmap = block_num/BITMAP_BLOCK;
static long long bitmap[_bitmap];
static void *mem[block_num];

void *label_bitmap(int number)
{
    bitmap[number/BITMAP_BLOCK] |= 1 << (number%BITMAP_BLOCK);
}

void *unlabel_bitmap(int number)
{
    bitmap[number/BITMAP_BLOCK] &= !(1 << (number%BITMAP_BLOCK));
}

int find_block()
{
    long long tmp;
    int i;
    int k;
    for(i=0; i<_bitmap; i++)
        if(bitmap[i] != -1)
        {
            tmp = bitmap[i] ^ -1;
            k = 0;
            while(1)
            {
                if(tmp&1)
                    return i * BITMAP_BLOCK + k;
                tmp = tmp >> 2;
                k ++;
            }
        }
}
struct block_array * allocate(int size)
{
    int i;
    int num;
    int number = size/BLOCK_SIZE;
    struct block_array content[number/_bitmap];
    struct block_array *p = content;
    for(i=0;i<number/_bitmap;i++)
    {
        if(i==number/_bitmap)
            content[i].next = NULL;
        else
            *content[i].next = content[i+1];
    }
    for(i = 0; i < number; i++)
    {
        num = find_block();
        mem[num] = mmap(0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        label_bitmap(num);
        content[i/_bitmap].bid[i%_bitmap] = num;
    }
    for(;i<(number/_bitmap+1)*_bitmap;i++)
        content[i/_bitmap].bid[i%_bitmap] = -1;
    return p;
}

void allo_free (struct block_array * ptr)
{
    int i, j, num;
    j = 0;
    while(1)
    {
        for(i=0;i<_bitmap;i++)
        {
            num = ptr[j].bid[i];
            if(num < 0)
                return;
            munmap(mem[num], BLOCK_SIZE);
        }
        j++;
    }
}

struct block_array *reallocate(struct block_array *ptr, int size)
{
    int number_1 = size/BLOCK_SIZE;
    int offset, k;
    int i = 0;
    int num;
    struct block_array * p = ptr;
    while(p->next != NULL)
    {
        p = p->next;
        i++;
    }
    k = i;
    offset = number_1 - k * _bitmap;
    struct block_array content[offset/_bitmap];
    p->next = &content[0];
    for(i=0;i<offset/_bitmap;i++)
    {
        if(i==offset/_bitmap)
            content[i].next = NULL;
        else
            *content[i].next = content[i+1];
    }
    for(i=0;p->bid[i]>=0;i++);
    for(; i <= offset; i++)
    {
        num = find_block();
        mem[num] = mmap(0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        label_bitmap(num);
        content[i/_bitmap].bid[i%_bitmap] = num;
    }
    for(;i<(offset/_bitmap+1)*_bitmap;i++)
        content[i/_bitmap].bid[i%_bitmap] = -1;
}

static struct filenode* root = NULL;

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
    struct filenode *new = (struct filenode *)allocate(sizeof(struct filenode));
    new->filename = (char *)allocate(strlen(filename) + 1);
    memcpy(new->filename, filename, strlen(filename) + 1);
    new->st = (struct stat *)allocate(sizeof(struct stat));
    memcpy(new->st, st, sizeof(struct stat));
    new->next = root;
    new->content = NULL;
    root = new;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
    size_t blocknr = sizeof(mem) / sizeof(mem[0]);
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
    }
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
    node->content = reallocate(node->content, offset + size);
    memcpy(node->content + offset, buf, size);
    return size;
}

static int oshfs_truncate(const char *path, off_t size)
{
    struct filenode *node = get_filenode(path);
    node->st->st_size = size;
    node->content = reallocate(node->content, size);
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
    allo_free(node->st);
    allo_free(node->content);
    allo_free(node);
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
