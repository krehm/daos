/**
 * (C) Copyright 2016-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * VOS object table definition
 * vos/vos_obj_index.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree.h>
#include <daos/object.h>
#include <daos_types.h>
#include <vos_internal.h>
#include <vos_obj.h>

/** iterator for oid */
struct vos_oi_iter {
	/** embedded VOS common iterator */
	struct vos_iterator	oit_iter;
	/** Handle of iterator */
	daos_handle_t		oit_hdl;
	/** condition of the iterator: epoch range */
	daos_epoch_range_t	oit_epr;
	/** Reference to the container */
	struct vos_container	*oit_cont;
};

static int
oi_hkey_size(void)
{
	return sizeof(daos_unit_oid_t);
}

static int
oi_rec_msize(int alloc_overhead)
{
	return alloc_overhead + sizeof(struct vos_obj_df);
}

static void
oi_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(daos_unit_oid_t));

	memcpy(hkey, key_iov->iov_buf, sizeof(daos_unit_oid_t));
}

static int
oi_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	daos_unit_oid_t *oid1 = (daos_unit_oid_t *)&rec->rec_hkey[0];
	daos_unit_oid_t	*oid2 = (daos_unit_oid_t *)hkey;

	return dbtree_key_cmp_rc(memcmp(oid1, oid2, sizeof(*oid1)));
}

static int
oi_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	     d_iov_t *val_iov, struct btr_record *rec)
{
	struct vos_obj_df	*obj;
	daos_unit_oid_t		*key;
	umem_off_t		 obj_off;
	int			 rc;

	/* Allocate a PMEM value of type vos_obj_df */
	obj_off = umem_zalloc(&tins->ti_umm, sizeof(struct vos_obj_df));
	if (UMOFF_IS_NULL(obj_off))
		return -DER_NOSPACE;

	obj = umem_off2ptr(&tins->ti_umm, obj_off);

	D_ASSERT(key_iov->iov_len == sizeof(*key));
	key = key_iov->iov_buf;

	obj->vo_sync	= 0;
	obj->vo_id	= *key;
	rc = ilog_create(&tins->ti_umm, &obj->vo_ilog, true);
	if (rc != 0) {
		D_ERROR("Failure to create incarnation log: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	d_iov_set(val_iov, obj, sizeof(struct vos_obj_df));
	rec->rec_off = obj_off;

	D_DEBUG(DB_TRACE, "alloc "DF_UOID" rec "DF_X64"\n",
		DP_UOID(obj->vo_id), rec->rec_off);
	return 0;
}

static int
oi_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct vos_obj_df	*obj;
	int			 rc;

	obj = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	rc = ilog_destroy(&tins->ti_umm, &obj->vo_ilog);
	if (rc != 0) {
		D_ERROR("Failed to destroy incarnation log: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	if (obj->vo_dtx_shares > 0) {
		D_ERROR("There are some unknown DTXs (%d) share the obj rec\n",
			obj->vo_dtx_shares);
		return -DER_BUSY;
	}
	D_ASSERT(tins->ti_priv);
	return gc_add_item((struct vos_pool *)tins->ti_priv, GC_OBJ,
			   rec->rec_off, 0);
}

static int
oi_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		 d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct vos_obj_df	*obj;

	obj = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	D_DEBUG(DB_TRACE, "fetch "DF_UOID" rec "DF_X64"\n",
		DP_UOID(obj->vo_id), rec->rec_off);

	D_ASSERT(val_iov != NULL);
	d_iov_set(val_iov, obj, sizeof(struct vos_obj_df));
	return 0;
}

static int
oi_rec_update(struct btr_instance *tins, struct btr_record *rec,
		  d_iov_t *key, d_iov_t *val)
{
	D_ASSERTF(0, "Should never been called\n");
	return 0;
}

static btr_ops_t oi_btr_ops = {
	.to_rec_msize		= oi_rec_msize,
	.to_hkey_size		= oi_hkey_size,
	.to_hkey_gen		= oi_hkey_gen,
	.to_hkey_cmp		= oi_hkey_cmp,
	.to_rec_alloc		= oi_rec_alloc,
	.to_rec_free		= oi_rec_free,
	.to_rec_fetch		= oi_rec_fetch,
	.to_rec_update		= oi_rec_update,
};

/**
 * Locate a durable object in OI table.
 */
int
vos_oi_find(struct vos_container *cont, daos_unit_oid_t oid,
	    struct vos_obj_df **obj_p)
{
	d_iov_t			key_iov;
	d_iov_t			val_iov;
	int			rc;

	*obj_p = NULL;

	d_iov_set(&key_iov, &oid, sizeof(oid));
	d_iov_set(&val_iov, NULL, 0);

	rc = dbtree_fetch(cont->vc_btr_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key_iov, NULL, &val_iov);
	if (rc == 0) {
		struct vos_obj_df *obj = val_iov.iov_buf;

		D_ASSERT(daos_unit_obj_id_equal(obj->vo_id, oid));

		*obj_p = obj;
	}
	return rc;
}

/**
 * Locate a durable object in OI table, or create it if it's not found
 */
int
vos_oi_find_alloc(struct vos_container *cont, daos_unit_oid_t oid,
		  daos_epoch_t epoch, uint32_t intent,
		  struct vos_obj_df **obj_p)
{
	struct vos_obj_df	*obj = NULL;
	daos_unit_oid_t		 key;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	daos_handle_t		 loh;
	int			 rc;

	*obj_p = NULL;

	D_DEBUG(DB_TRACE, "Lookup obj "DF_UOID" in the OI table.\n",
		DP_UOID(oid));

	rc = vos_oi_find(cont, oid, &obj);
	if (rc == 0)
		goto update_log;
	if (rc != -DER_NONEXIST)
		return rc;

	/* Object ID not found insert it to the OI tree */
	D_DEBUG(DB_TRACE, "Object "DF_UOID" not found adding it..\n",
		DP_UOID(oid));

	key = oid;
	d_iov_set(&key_iov, &key, sizeof(key));

	rc = dbtree_upsert(cont->vc_btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT,
			   &key_iov, &val_iov);
	if (rc) {
		D_ERROR("Failed to update Key for Object index\n");
		return rc;
	}

	obj = val_iov.iov_buf;
update_log:
	rc = ilog_open(vos_cont2umm(cont), &obj->vo_ilog, &loh);
	if (rc != 0)
		return rc;

	rc = ilog_update(loh, epoch, false);

	ilog_close(loh);

	if (rc == 0)
		*obj_p = obj;

	return rc;
}

/**
 * Punch a durable object, it will generate a new incarnation with the same
 * ID in OI table.
 */
int
vos_oi_punch(struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_t epoch, uint32_t flags, struct vos_obj_df *obj)
{
	daos_handle_t	loh = DAOS_HDL_INVAL;
	/* create a new incarnation for the punch */
	int		 rc = 0;

	D_DEBUG(DB_TRACE, "Punch obj "DF_UOID", epoch="DF_U64".\n",
		DP_UOID(oid), epoch);

	rc = ilog_open(vos_cont2umm(cont), &obj->vo_ilog, &loh);
	if (rc != 0)
		return rc;

	rc = ilog_update(loh, epoch, true);

	ilog_close(loh);

	if (rc != 0)
		D_ERROR("Failed to punch object, "DF_RC"\n", DP_RC(rc));
	return rc;
}

/**
 * Delete a durable object
 *
 * NB: this operation is not going to be part of distributed transaction,
 * it is only used by rebalance and reintegration.
 *
 * XXX: this only deletes the latest incarnation of the object, but the
 * ongoing work (incarnation log) will change it and there will be only
 * one incarnation for each object.
 */
int
vos_oi_delete(struct vos_container *cont, daos_unit_oid_t oid)
{
	daos_unit_oid_t	key;
	d_iov_t		key_iov;
	int		rc = 0;

	D_DEBUG(DB_TRACE, "Delete obj "DF_UOID"\n", DP_UOID(oid));

	key = oid;
	d_iov_set(&key_iov, &key, sizeof(key));

	rc = dbtree_delete(cont->vc_btr_hdl, BTR_PROBE_EQ, &key_iov,
			   cont->vc_pool);
	if (rc == -DER_NONEXIST)
		return 0;

	if (rc != 0) {
		D_ERROR("Failed to delete object, rc=%s\n", d_errstr(rc));
		return rc;
	}
	return 0;
}

static struct vos_oi_iter *
iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_oi_iter, oit_iter);
}

static int
oi_iter_fini(struct vos_iterator *iter)
{
	int			rc  = 0;
	struct vos_oi_iter	*oiter = NULL;

	/** iter type should be VOS_ITER_OBJ */
	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	oiter = iter2oiter(iter);

	if (!daos_handle_is_inval(oiter->oit_hdl)) {
		rc = dbtree_iter_finish(oiter->oit_hdl);
		if (rc)
			D_ERROR("oid_iter_fini failed:%d\n", rc);
	}

	if (oiter->oit_cont != NULL)
		vos_cont_decref(oiter->oit_cont);

	D_FREE(oiter);
	return rc;
}

static int
oi_iter_nested_tree_fetch(struct vos_iterator *iter, vos_iter_type_t type,
			  struct vos_iter_info *info)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	struct vos_obj_df	*obj;
	d_iov_t		 rec_iov;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	if (type != VOS_ITER_DKEY) {
		D_DEBUG(DB_TRACE, "Expected VOS_ITER_DKEY nested iterator type,"
			" got %d\n", type);
		return -DER_INVAL;
	}

	d_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, NULL);
	if (rc != 0) {
		if (rc == -DER_INPROGRESS)
			D_DEBUG(DB_TRACE, "Cannot fetch oid infor because of "
				"conflict modification: %d\n", rc);
		else
			D_ERROR("Error while fetching oid info: %d\n", rc);
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_obj_df));
	obj = (struct vos_obj_df *)rec_iov.iov_buf;

	info->ii_oid = obj->vo_id;
	info->ii_punched = 0;
	/* Limit the bounds to this object incarnation */
	info->ii_epr.epr_lo = MAX(obj->vo_earliest, oiter->oit_epr.epr_lo);
	info->ii_epr.epr_hi = MIN(obj->vo_latest, oiter->oit_epr.epr_hi);
	info->ii_hdl = vos_cont2hdl(oiter->oit_cont);

	return 0;
}

static int
oi_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
	   struct vos_iterator **iter_pp)
{
	struct vos_oi_iter	*oiter = NULL;
	struct vos_container	*cont = NULL;
	int			rc = 0;

	if (type != VOS_ITER_OBJ) {
		D_ERROR("Expected Type: %d, got %d\n",
			VOS_ITER_OBJ, type);
		return -DER_INVAL;
	}

	cont = vos_hdl2cont(param->ip_hdl);
	if (cont == NULL)
		return -DER_INVAL;

	D_ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	oiter->oit_iter.it_type = type;
	oiter->oit_epr  = param->ip_epr;
	oiter->oit_cont = cont;
	vos_cont_addref(cont);

	if (param->ip_flags & VOS_IT_FOR_PURGE)
		oiter->oit_iter.it_for_purge = 1;
	if (param->ip_flags & VOS_IT_FOR_REBUILD)
		oiter->oit_iter.it_for_rebuild = 1;

	rc = dbtree_iter_prepare(cont->vc_btr_hdl, 0, &oiter->oit_hdl);
	if (rc)
		D_GOTO(exit, rc);

	*iter_pp = &oiter->oit_iter;
	return 0;
exit:
	oi_iter_fini(&oiter->oit_iter);
	return rc;
}

/**
 * This function checks if the current object can match the condition, it
 * returns immediately on true, otherwise it will move the iterator cursor
 * to the object matching the condition.
 */
static int
oi_iter_match_probe(struct vos_iterator *iter)
{
	struct vos_oi_iter	*oiter	= iter2oiter(iter);
	daos_epoch_range_t	*epr	= &oiter->oit_epr;
	char			*str	= NULL;
	int			 rc;

	if (oiter->oit_epr.epr_lo == 0 &&
	    oiter->oit_epr.epr_hi == DAOS_EPOCH_MAX)
		goto out; /* no condition so it's a match */

	while (1) {
		struct vos_obj_df *obj;
		int		   probe;
		d_iov_t	   iov;

		rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &iov, NULL);
		if (rc != 0) {
			str = "fetch";
			goto failed;
		}

		D_ASSERT(iov.iov_len == sizeof(struct vos_obj_df));
		obj = (struct vos_obj_df *)iov.iov_buf;

		probe = 0;
		if (obj->vo_earliest > epr->epr_hi) {
			/* NB: The object was created/updated only after the
			 * range in the condition so the object and all
			 * remaining incarnations of it can be skipped.
			 */
			probe = BTR_PROBE_GT;
		//	hkey.oi_epc = DAOS_EPOCH_MAX;
		} else if (obj->vo_oi_attr & VOS_OI_PUNCHED &&
			   obj->vo_latest <= epr->epr_lo) {
			/* NB: Object was punched before our range.
			 * Probe again using our range condition to select
			 * either the next object or a subsequent incarnation
			 * of the current one.
			 */
			probe = BTR_PROBE_GT;
		//	hkey.oi_epc = epr->epr_lo;
		} else {
			/* Matches the condition. */
			break;
		}

		//hkey.oi_oid = obj->vo_id;
		//d_iov_set(&iov, &hkey, sizeof(hkey));
		rc = dbtree_iter_probe(oiter->oit_hdl, probe,
				       vos_iter_intent(iter), &iov, NULL);
		if (rc != 0) {
			str = "probe";
			goto failed;
		}
	}
 out:
	return 0;
 failed:
	if (rc == -DER_NONEXIST)
		return 0; /* end of iteration, not a failure */

	D_ERROR("iterator %s failed, rc=%d\n", str, rc);
	return rc;
}

static int
oi_iter_probe(struct vos_iterator *iter, daos_anchor_t *anchor)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	dbtree_probe_opc_t	 opc;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	rc = dbtree_iter_probe(oiter->oit_hdl, opc, vos_iter_intent(iter), NULL,
			       anchor);
	if (rc)
		D_GOTO(out, rc);

	/* NB: these probe cannot guarantee the returned entry is within
	 * the condition epoch range.
	 */
	rc = oi_iter_match_probe(iter);
 out:
	return rc;
}

static int
oi_iter_next(struct vos_iterator *iter)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);
	rc = dbtree_iter_next_with_intent(oiter->oit_hdl,
					  vos_iter_intent(iter));
	if (rc)
		D_GOTO(out, rc);

	rc = oi_iter_match_probe(iter);
 out:
	return rc;
}

static int
oi_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
	      daos_anchor_t *anchor)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	struct vos_obj_df	*obj;
	d_iov_t		 rec_iov;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	d_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, anchor);
	if (rc != 0) {
		if (rc == -DER_INPROGRESS)
			D_DEBUG(DB_TRACE, "Cannot fetch oid info because of "
				"conflict modification: %d\n", rc);
		else
			D_ERROR("Error while fetching oid info: %d\n", rc);
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_obj_df));
	obj = (struct vos_obj_df *)rec_iov.iov_buf;

	it_entry->ie_oid = obj->vo_id;
	if (obj->vo_oi_attr & VOS_OI_PUNCHED)
		it_entry->ie_epoch = obj->vo_latest;
	else
		it_entry->ie_epoch = DAOS_EPOCH_MAX;
	it_entry->ie_child_type = VOS_ITER_DKEY;
	return 0;
}

static int
oi_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_oi_iter	*oiter = iter2oiter(iter);
	int			rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	rc = vos_tx_begin(vos_cont2umm(oiter->oit_cont));
	if (rc != 0)
		goto exit;

	rc = dbtree_iter_delete(oiter->oit_hdl, args);

	rc = vos_tx_end(vos_cont2umm(oiter->oit_cont), rc);

	if (rc != 0)
		D_ERROR("Failed to delete oid entry: %d\n", rc);
exit:
	return rc;
}

struct vos_iter_ops vos_oi_iter_ops = {
	.iop_prepare		= oi_iter_prep,
	.iop_nested_tree_fetch	= oi_iter_nested_tree_fetch,
	.iop_finish		= oi_iter_fini,
	.iop_probe		= oi_iter_probe,
	.iop_next		= oi_iter_next,
	.iop_fetch		= oi_iter_fetch,
	.iop_delete		= oi_iter_delete,
};

/**
 * Internal usage APIs
 * For use from container APIs and init APIs
 */
int
vos_obj_tab_register()
{
	int	rc;

	D_DEBUG(DB_DF, "Registering class for OI table Class: %d\n",
		VOS_BTR_OBJ_TABLE);

	rc = dbtree_class_register(VOS_BTR_OBJ_TABLE, 0, &oi_btr_ops);
	if (rc)
		D_ERROR("dbtree create failed\n");
	return rc;
}
