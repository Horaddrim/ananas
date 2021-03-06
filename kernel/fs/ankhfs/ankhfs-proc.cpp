#include <ananas/types.h>
#include <ananas/error.h>
#include <ananas/vfs.h>
#include <ananas/vfs/core.h>
#include <ananas/vfs/generic.h>
#include <ananas/process.h>
#include <ananas/procinfo.h>
#include <ananas/vm.h>
#include <ananas/vmspace.h>
#include <ananas/trace.h>
#include <ananas/lib.h>
#include "proc.h"
#include "support.h"

TRACE_SETUP;

namespace Ananas {
namespace Process {

extern mutex_t process_mtx;
extern struct PROCESS_QUEUE process_all;

} // namespace Process

namespace AnkhFS {

namespace {

constexpr unsigned int subName = 1;
constexpr unsigned int subVmSpace = 2;

struct DirectoryEntry proc_entries[] = {
	{ "name", make_inum(SS_Proc, 0, subName) },
	{ "vmspace", make_inum(SS_Proc, 0, subVmSpace) },
	{ NULL, 0 }
};

errorcode_t
HandleReadDir_Proc_Root(struct VFS_FILE* file, void* dirents, size_t* len)
{
	struct FetchEntry : IReadDirCallback {
		bool FetchNextEntry(char* entry, size_t maxLength, ino_t& inum) override {
			if (currentProcess == nullptr)
				return false;

			// XXX we should lock currentProcess here
			snprintf(entry, maxLength, "%d", static_cast<int>(currentProcess->p_pid));
			inum = make_inum(SS_Proc, currentProcess->p_pid, 0);
			currentProcess = LIST_NEXT_IP(currentProcess, all);
			return true;
		}

		process_t* currentProcess = LIST_HEAD(&Process::process_all);
	};

	// Fill the root directory with one directory per process ID
	mutex_lock(&Process::process_mtx);
	FetchEntry entryFetcher;
	errorcode_t err = HandleReadDir(file, dirents, len, entryFetcher);
	mutex_unlock(&Process::process_mtx);
	return err;
}

class ProcSubSystem : public IAnkhSubSystem
{
public:
	errorcode_t HandleReadDir(struct VFS_FILE* file, void* dirents, size_t* len) override
	{
		struct VFS_INODE* inode = file->f_dentry->d_inode;
		ino_t inum = inode->i_inum;

		if (inum_to_id(inum) == 0)
			return HandleReadDir_Proc_Root(file, dirents, len);

		return AnkhFS::HandleReadDir(file, dirents, len, proc_entries[0], inum_to_id(inum));
	}

	errorcode_t FillInode(struct VFS_INODE* inode, ino_t inum) override
	{
		if (inum_to_sub(inum) == 0) {
			inode->i_sb.st_mode |= S_IFDIR;
		} else {
			inode->i_sb.st_mode |= S_IFREG;
		}
		return ananas_success();
	}

	errorcode_t HandleRead(struct VFS_FILE* file, void* buf, size_t* len) override
	{
		ino_t inum = file->f_dentry->d_inode->i_inum;

		pid_t pid = static_cast<pid_t>(inum_to_id(inum));
		process_t* p = process_lookup_by_id_and_ref(pid);
		if (p == nullptr)
			return ANANAS_ERROR(IO);

		char result[256]; // XXX
		strcpy(result, "???");
		switch(inum_to_sub(inum)) {
			case subName: {
				if (p->p_info != nullptr)
					strncpy(result, p->p_info->pi_args, sizeof(result));
				break;
			}
			case subVmSpace: {
				if (p->p_vmspace != nullptr) {
					// XXX shouldn't we lock something here?'
					char* r = result;
					LIST_FOREACH(&p->p_vmspace->vs_areas, va, vmarea_t) {
						snprintf(r, sizeof(result) - (r - result), "%p %p %c%c%c\n",
						 reinterpret_cast<void*>(va->va_virt), reinterpret_cast<void*>(va->va_len),
						 (va->va_flags & VM_FLAG_READ) ? 'r' : '-',
						 (va->va_flags & VM_FLAG_WRITE) ? 'w' : '-',
						 (va->va_flags & VM_FLAG_EXECUTE) ? 'x' : '-');
						r += strlen(r);
					}
				}
				break;
			}
		}
		result[sizeof(result) - 1] = '\0';
		process_deref(p);
		return AnkhFS::HandleRead(file, buf, len, result);
	}
};

} // unnamed namespace

IAnkhSubSystem& GetProcSubSystem()
{
	static ProcSubSystem procSubSystem;
	return procSubSystem;
}

} // namespace AnkhFS
} // namespace Ananas

/* vim:set ts=2 sw=2: */
