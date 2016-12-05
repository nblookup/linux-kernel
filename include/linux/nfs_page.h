/*
 * linux/include/linux/nfs_page.h
 *
 * Copyright (C) 2000 Trond Myklebust
 *
 * NFS page cache wrapper.
 */

#ifndef _LINUX_NFS_PAGE_H
#define _LINUX_NFS_PAGE_H


#include <linux/list.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/sunrpc/auth.h>
#include <linux/nfs_xdr.h>

/*
 * Valid flags for a dirty buffer
 */
#define PG_BUSY			0x0001

struct nfs_page {
	struct list_head	wb_hash,	/* Inode */
				wb_list,	/* Defines state of page: */
				*wb_list_head;	/*      read/write/commit */
	struct file		*wb_file;
	struct inode		*wb_inode;
	struct rpc_cred		*wb_cred;
	struct page		*wb_page;	/* page to read in/write out */
	struct wait_queue	*wb_wait;	/* wait queue */
	unsigned long		wb_timeout;	/* when to read/write/commit */
	unsigned int		wb_offset,	/* Offset of read/write */
				wb_bytes,	/* Length of request */
				wb_count,	/* reference count */
				wb_flags;
	struct nfs_writeverf	wb_verf;	/* Commit cookie */
};

#define NFS_WBACK_BUSY(req)	((req)->wb_flags & PG_BUSY)

extern	struct nfs_page *nfs_create_request(struct file *file,
					    struct inode *inode,
					    struct page *page,
					    unsigned int offset,
					    unsigned int count);
extern	void nfs_release_request(struct nfs_page *req);


extern	void nfs_list_add_request(struct nfs_page *req,
				  struct list_head *head);
extern	void nfs_list_remove_request(struct nfs_page *req);

extern	int nfs_scan_list_timeout(struct list_head *head,
				  struct list_head *dst,
				  struct inode *inode);
extern	int nfs_scan_list(struct list_head *src, struct list_head *dst,
			  struct file *file, unsigned long idx_start,
			  unsigned int npages);
extern	int nfs_coalesce_requests(struct list_head *src, struct list_head *dst,
				  unsigned int maxpages);

/*
 * Lock the page of an asynchronous request
 */
static __inline__ int
nfs_lock_request(struct nfs_page *req)
{
	if (NFS_WBACK_BUSY(req))
		return 0;
	req->wb_count++;
	req->wb_flags |= PG_BUSY;
	return 1;
}

static __inline__ void
nfs_unlock_request(struct nfs_page *req)
{
	if (!NFS_WBACK_BUSY(req)) {
		printk(KERN_ERR "NFS: Invalid unlock attempted\n");
		return;
	}
	req->wb_flags &= ~PG_BUSY;
	wake_up(&req->wb_wait);
	nfs_release_request(req);
}

static __inline__ struct nfs_page *
nfs_list_entry(struct list_head *head)
{
	return list_entry(head, struct nfs_page, wb_list);
}

static __inline__ struct nfs_page *
nfs_inode_wb_entry(struct list_head *head)
{
	return list_entry(head, struct nfs_page, wb_hash);
}

#endif /* _LINUX_NFS_PAGE_H */
