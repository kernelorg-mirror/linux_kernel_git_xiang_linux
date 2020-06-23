// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef XFS_IUNLINK_ITEM_H
#define XFS_IUNLINK_ITEM_H	1

struct xfs_trans;
struct xfs_inode;

/* in memory log item structure */
struct xfs_iunlink_item {
	struct xfs_log_item	iu_item;
	struct xfs_imap		iu_imap;
	xfs_ino_t		iu_ino;
	xfs_agino_t		iu_next_unlinked;
};

extern kmem_zone_t *xfs_iunlink_zone;

void xfs_iunlink_log(struct xfs_trans *tp, struct xfs_inode *ip);

#endif	/* XFS_IUNLINK_ITEM_H */
