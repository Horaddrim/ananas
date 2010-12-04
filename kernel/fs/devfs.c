/*
 * Ananas devfs driver.
 *
 * This filesystem is constructed, i.e. it does not have a backing device but
 * will rather generate all data on the fly. It will take the device tree and
 * make it accessible to third party programs.
 */
#include <ananas/types.h>
#include <ananas/error.h>
#include <ananas/vfs.h>
#include <ananas/trace.h>
#include <ananas/lib.h>
#include <ananas/mm.h>

TRACE_SETUP;

#define DEVFS_BLOCK_SIZE 512
#define DEVFS_ROOTINODE_FSOP 1

struct DEVFS_INODE_PRIVDATA {
	device_t device;
};

static struct VFS_INODE*
devfs_alloc_inode(struct VFS_MOUNTED_FS* fs)
{
	struct VFS_INODE* inode = vfs_make_inode(fs);
	if (inode == NULL)
		return NULL;
	inode->privdata = kmalloc(sizeof(struct DEVFS_INODE_PRIVDATA));
	memset(inode->privdata, 0, sizeof(struct DEVFS_INODE_PRIVDATA));
	return inode;
}

static void
devfs_destroy_inode(struct VFS_INODE* inode)
{
	kfree(inode->privdata);
	vfs_destroy_inode(inode);
}

static errorcode_t
devfs_readdir_root(struct VFS_FILE* file, void* dirents, size_t* len)
{
	size_t left = *len, written = 0;
	uint32_t inum = DEVFS_ROOTINODE_FSOP + 1;

	struct DEVICE* dev = DQUEUE_HEAD(device_get_queue());
	for (unsigned int i = 0; i < file->offset && dev != NULL; i++) {
		dev = DQUEUE_NEXT(dev); inum++;
	}

	while (left > 0 && dev != NULL) {
		char devname[128 /* XXX */];
		sprintf(devname, "%s%u", dev->name, dev->unit);
		int filled = vfs_filldirent(&dirents, &left, (const void*)&inum, file->inode->fs->fsop_size, devname, strlen(devname));
		if (!filled) {
			/* out of space! */
			break;
		}
		written += filled; inum++;
		file->offset++; dev = DQUEUE_NEXT(dev);
	}

	*len = written;
	return ANANAS_ERROR_OK;
}

static struct VFS_INODE_OPS devfs_rootdir_ops = {
	.readdir = devfs_readdir_root,
	.lookup = vfs_generic_lookup
};

static errorcode_t
devfs_read(struct VFS_FILE* file, void* buf, size_t* len)
{
	kprintf("devfs_read: todo\n");
	return ANANAS_ERROR(IO);
}

static errorcode_t
devfs_write(struct VFS_FILE* file, void* buf, size_t* len)
{
	kprintf("devfs_write: todo\n");
	return ANANAS_ERROR(IO);
}

static struct VFS_INODE_OPS devfs_file_ops = {
	.read = devfs_read,
	.write = devfs_write,
};

static errorcode_t
devfs_read_inode(struct VFS_INODE* inode, void* fsop)
{
	uint32_t ino = *(uint32_t*)fsop;

	inode->sb.st_ino = ino;
	inode->sb.st_mode = 0444;
	inode->sb.st_nlink = 1;
	inode->sb.st_uid = 0;
	inode->sb.st_gid = 0;
	inode->sb.st_atime = 0;
	inode->sb.st_mtime = 0;
	inode->sb.st_ctime = 0;
	inode->sb.st_size = 0;

	if (ino == DEVFS_ROOTINODE_FSOP) {
		inode->sb.st_mode |= S_IFDIR | 0111;
		inode->iops = &devfs_rootdir_ops;
	} else {
		inode->iops = &devfs_file_ops;
	}
	return ANANAS_ERROR_NONE;
}

static errorcode_t
devfs_mount(struct VFS_MOUNTED_FS* fs)
{
	fs->block_size = DEVFS_BLOCK_SIZE;
	fs->fsop_size = sizeof(uint32_t);
	fs->root_inode = devfs_alloc_inode(fs);
	uint32_t root_fsop = DEVFS_ROOTINODE_FSOP;
	return vfs_get_inode(fs, &root_fsop, &fs->root_inode);
}

struct VFS_FILESYSTEM_OPS fsops_devfs = {
	.mount = devfs_mount,
	.alloc_inode = devfs_alloc_inode,
	.destroy_inode = devfs_destroy_inode,
	.read_inode = devfs_read_inode
};

/* vim:set ts=2 sw=2: */