// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_iunlink_item.h"
#include "xfs_trace.h"

kmem_zone_t	*xfs_iunlink_zone;

static inline struct xfs_iunlink_item *IUL_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_iunlink_item, iu_item);
}

static void
xfs_iunlink_item_release(
	struct xfs_log_item	*lip)
{
	kmem_cache_free(xfs_iunlink_zone, IUL_ITEM(lip));
}


static uint64_t
xfs_iunlink_item_sort(
	struct xfs_log_item	*lip)
{
	return IUL_ITEM(lip)->iu_ino;
}

/*
 * On precommit, we grab the inode cluster buffer for the inode number
 * we were passed, then update the next unlinked field for that inode in
 * the buffer and log the buffer. This ensures that the inode cluster buffer
 * was logged in the correct order w.r.t. other inode cluster buffers.
 *
 * Note: if the inode cluster buffer is marked stale, this transaction is
 * actually freeing the inode cluster. In that case, do not relog the buffer
 * as this removes the stale state from it. That then causes the post-commit
 * processing that is dependent on the cluster buffer being stale to go wrong
 * and we'll leave stale inodes in the AIL that cannot be removed, hanging the
 * log.
 */
static int
xfs_iunlink_item_precommit(
	struct xfs_trans	*tp,
	struct xfs_log_item	*lip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_iunlink_item	*iup = IUL_ITEM(lip);
	xfs_agnumber_t		agno = XFS_INO_TO_AGNO(mp, iup->iu_ino);
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, iup->iu_ino);
	struct xfs_dinode	*dip;
	struct xfs_buf		*bp;
	int			offset;
	int			error;

	error = xfs_imap_to_bp(mp, tp, &iup->iu_imap, &dip, &bp, 0);
	if (error)
		goto out_remove;

	trace_xfs_iunlink_update_dinode(mp, agno, agino,
			be32_to_cpu(dip->di_next_unlinked),
			iup->iu_next_unlinked);

	/*
	 * Don't bother updating the unlinked field on stale buffers as
	 * it will never get to disk anyway.
	 */
	if (bp->b_flags & XBF_STALE)
		goto out_remove;

	dip->di_next_unlinked = cpu_to_be32(iup->iu_next_unlinked);
	offset = iup->iu_imap.im_boffset +
			offsetof(struct xfs_dinode, di_next_unlinked);

	/* need to recalc the inode CRC if appropriate */
	xfs_dinode_calc_crc(mp, dip);
	xfs_trans_inode_buf(tp, bp);
	xfs_trans_log_buf(tp, bp, offset, offset + sizeof(xfs_agino_t) - 1);

out_remove:
	/*
	 * This log item only exists to perform this action. We now remove
	 * it from the transaction and free it as it should never reach the
	 * CIL.
	 */
	list_del(&lip->li_trans);
	xfs_iunlink_item_release(lip);
	return error;
}

static const struct xfs_item_ops xfs_iunlink_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED,
	.iop_release	= xfs_iunlink_item_release,
	.iop_sort	= xfs_iunlink_item_sort,
	.iop_precommit	= xfs_iunlink_item_precommit,
};


/*
 * Initialize the inode log item for a newly allocated (in-core) inode.
 *
 * Inode extents can only reside within an AG. Hence specify the starting
 * block for the inode chunk by offset within an AG as well as the
 * length of the allocated extent.
 *
 * This joins the item to the transaction and marks it dirty so
 * that we don't need a separate call to do this, nor does the
 * caller need to know anything about the iunlink item.
 */
void
xfs_iunlink_log(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	struct xfs_iunlink_item	*iup;

	iup = kmem_zone_zalloc(xfs_iunlink_zone, 0);

	xfs_log_item_init(tp->t_mountp, &iup->iu_item, XFS_LI_IUNLINK,
			  &xfs_iunlink_item_ops);

	iup->iu_ino = ip->i_ino;
	iup->iu_next_unlinked = ip->i_next_unlinked;
	iup->iu_imap = ip->i_imap;

	xfs_trans_add_item(tp, &iup->iu_item);
	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &iup->iu_item.li_flags);
}
