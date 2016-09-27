/*
 * Copyright 2011 Tresys Technology, LLC. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 * 
 *    2. Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the documentation
 *       and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY TRESYS TECHNOLOGY, LLC ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL TRESYS TECHNOLOGY, LLC OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of Tresys Technology, LLC.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sepol/policydb/conditional.h>

#include "cil_internal.h"
#include "cil_flavor.h"
#include "cil_log.h"
#include "cil_mem.h"
#include "cil_tree.h"
#include "cil_list.h"
#include "cil_build_ast.h"
#include "cil_resolve_ast.h"
#include "cil_reset_ast.h"
#include "cil_copy_ast.h"
#include "cil_verify.h"
#include "cil_strpool.h"
#include "cil_symtab.h"

struct cil_args_resolve {
	struct cil_db *db;
	enum cil_pass pass;
	uint32_t *changed;
	char *last_resolved_name;
	struct cil_tree_node *optstack;
	struct cil_tree_node *boolif;
	struct cil_tree_node *macro;
	struct cil_tree_node *blockstack;
	struct cil_list *sidorder_lists;
	struct cil_list *classorder_lists;
	struct cil_list *unordered_classorder_lists;
	struct cil_list *catorder_lists;
	struct cil_list *sensitivityorder_lists;
	struct cil_list *in_list;
};

static struct cil_name * __cil_insert_name(struct cil_db *db, hashtab_key_t key, struct cil_tree_node *ast_node)
{
	/* Currently only used for typetransition file names.
	   But could be used for any string that is passed as a parameter.
	*/
	struct cil_tree_node *parent = ast_node->parent;
	struct cil_macro *macro = NULL;
	struct cil_name *name;
	symtab_t *symtab;
	enum cil_sym_index sym_index;
	struct cil_symtab_datum *datum = NULL;

	cil_flavor_to_symtab_index(CIL_NAME, &sym_index);
	symtab = &((struct cil_root *)db->ast->root->data)->symtab[sym_index];

	cil_symtab_get_datum(symtab, key, &datum);
	if (datum != NULL) {
		return (struct cil_name *)datum;
	}

	if (parent->flavor == CIL_CALL) {
		struct cil_call *call = parent->data;
		macro = call->macro;	
	} else if (parent->flavor == CIL_MACRO) {
		macro = parent->data;
	}
	if (macro != NULL) {
		struct cil_list_item *item;
		cil_list_for_each(item, macro->params) {
			if (((struct cil_param*)item->data)->str == key) {
				return NULL;
			}
		}
	}

	cil_name_init(&name);
	cil_symtab_insert(symtab, key, (struct cil_symtab_datum *)name, ast_node);
	cil_list_append(db->names, CIL_NAME, name);

	return name;
}

static int __cil_resolve_perms(symtab_t *class_symtab, symtab_t *common_symtab, struct cil_list *perm_strs, struct cil_list **perm_datums)
{
	int rc = SEPOL_ERR;
	struct cil_list_item *curr;

	cil_list_init(perm_datums, perm_strs->flavor);

	cil_list_for_each(curr, perm_strs) {
		if (curr->flavor == CIL_LIST) {
			struct cil_list *sub_list;
			rc = __cil_resolve_perms(class_symtab, common_symtab, curr->data, &sub_list);
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Failed to resolve permission list\n");
				goto exit;
			}
			cil_list_append(*perm_datums, CIL_LIST, sub_list);
		} else if (curr->flavor == CIL_STRING) {
			struct cil_symtab_datum *perm_datum = NULL;
			rc = cil_symtab_get_datum(class_symtab, curr->data, &perm_datum);
			if (rc == SEPOL_ENOENT) {
				if (common_symtab) {
					rc = cil_symtab_get_datum(common_symtab, curr->data, &perm_datum);
				}
			}
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Failed to resolve permission %s\n", (char*)curr->data);
				goto exit;
			}
			cil_list_append(*perm_datums, CIL_DATUM, perm_datum);
		} else {
			cil_list_append(*perm_datums, curr->flavor, curr->data);
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_classperms(struct cil_tree_node *current, struct cil_classperms *cp, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_symtab_datum *datum = NULL;
	symtab_t *common_symtab = NULL;
	struct cil_class *class;

	rc = cil_resolve_name(current, cp->class_str, CIL_SYM_CLASSES, extra_args, &datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	class = (struct cil_class *)datum;

	if (class->common != NULL) {
		common_symtab = &class->common->perms;
	}

	cp->class = class;

	rc = __cil_resolve_perms(&class->perms, common_symtab, cp->perm_strs, &cp->perms);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_classperms_set(struct cil_tree_node *current, struct cil_classperms_set *cp_set, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_symtab_datum *datum = NULL;

	rc = cil_resolve_name(current, cp_set->set_str, CIL_SYM_CLASSPERMSETS, extra_args, &datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	cp_set->set = (struct cil_classpermission*)datum;

	/* This could be an anonymous classpermission */
	if (datum->name == NULL) {
		rc = cil_resolve_classperms_list(current, cp_set->set->classperms, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_classperms_list(struct cil_tree_node *current, struct cil_list *cp_list, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_list_item *curr;

	cil_list_for_each(curr, cp_list) {
		if (curr->flavor == CIL_CLASSPERMS) {
			rc = cil_resolve_classperms(current, curr->data, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		} else {
			rc = cil_resolve_classperms_set(current, curr->data, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_classpermissionset(struct cil_tree_node *current, struct cil_classpermissionset *cps, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_args_resolve *args = extra_args;
	struct cil_list_item *curr;
	struct cil_symtab_datum *datum;
	struct cil_classpermission *cp;

	rc = cil_resolve_name(current, cps->set_str, CIL_SYM_CLASSPERMSETS, args, &datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_resolve_classperms_list(current, cps->classperms, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cp = (struct cil_classpermission *)datum;

	if (cp->classperms == NULL) {
		cil_list_init(&cp->classperms, CIL_CLASSPERMS);
	}

	cil_list_for_each(curr, cps->classperms) {
		cil_list_append(cp->classperms, curr->flavor, curr->data);
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_type_used(struct cil_symtab_datum *datum)
{
	struct cil_typeattribute *attr = NULL;

	if (FLAVOR(datum) == CIL_TYPEATTRIBUTE) {
		attr = (struct cil_typeattribute*)datum;
		attr->used = CIL_TRUE;
	}

	return 0;
}

int cil_resolve_permissionx(struct cil_tree_node *current, struct cil_permissionx *permx, void *extra_args)
{
	struct cil_symtab_datum *obj_datum = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, permx->obj_str, CIL_SYM_CLASSES, extra_args, &obj_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	permx->obj = (struct cil_class*)obj_datum;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_avrule(struct cil_tree_node *current, void *extra_args)
{
	struct cil_args_resolve *args = extra_args;
	struct cil_db *db = NULL;

	struct cil_avrule *rule = current->data;
	struct cil_symtab_datum *src_datum = NULL;
	struct cil_symtab_datum *tgt_datum = NULL;
	struct cil_symtab_datum *permx_datum = NULL;
	int rc = SEPOL_ERR;

	if (args != NULL) {
		db = args->db;
	}

	rc = cil_resolve_name(current, rule->src_str, CIL_SYM_TYPES, args, &src_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	rule->src = src_datum;
	if (rule->rule_kind != CIL_AVRULE_NEVERALLOW) {
		cil_type_used(src_datum);
	}
		
	if (rule->tgt_str == CIL_KEY_SELF) {
		rule->tgt = db->selftype;
	} else {
		rc = cil_resolve_name(current, rule->tgt_str, CIL_SYM_TYPES, args, &tgt_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		rule->tgt = tgt_datum;
		if (rule->rule_kind != CIL_AVRULE_NEVERALLOW) {
			cil_type_used(tgt_datum);
		}
	}

	if (!rule->is_extended) {
		rc = cil_resolve_classperms_list(current, rule->perms.classperms, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	} else {
		if (rule->perms.x.permx_str != NULL) {
			rc = cil_resolve_name(current, rule->perms.x.permx_str, CIL_SYM_PERMX, args, &permx_datum);
			if (rc != SEPOL_OK) {
				goto exit;
			}
			rule->perms.x.permx = (struct cil_permissionx*)permx_datum;
		} else {
			rc = cil_resolve_permissionx(current, rule->perms.x.permx, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_type_rule(struct cil_tree_node *current, void *extra_args)
{
	struct cil_type_rule *rule = current->data;
	struct cil_symtab_datum *src_datum = NULL;
	struct cil_symtab_datum *tgt_datum = NULL;
	struct cil_symtab_datum *obj_datum = NULL;
	struct cil_symtab_datum *result_datum = NULL;
	struct cil_tree_node *result_node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, rule->src_str, CIL_SYM_TYPES, extra_args, &src_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	rule->src = src_datum;
	cil_type_used(src_datum);

	rc = cil_resolve_name(current, rule->tgt_str, CIL_SYM_TYPES, extra_args, &tgt_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	rule->tgt = tgt_datum;
	cil_type_used(tgt_datum);

	rc = cil_resolve_name(current, rule->obj_str, CIL_SYM_CLASSES, extra_args, &obj_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	rule->obj = (struct cil_class*)obj_datum;

	rc = cil_resolve_name(current, rule->result_str, CIL_SYM_TYPES, extra_args, &result_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	result_node = result_datum->nodes->head->data;

	if (result_node->flavor != CIL_TYPE) {
		cil_log(CIL_ERR, "Type rule result must be a type [%d]\n",result_node->flavor);
		rc = SEPOL_ERR;
		goto exit;
	}
	rule->result = result_datum;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_typeattributeset(struct cil_tree_node *current, void *extra_args)
{
	struct cil_typeattributeset *attrtypes = current->data;
	struct cil_symtab_datum *attr_datum = NULL;
	struct cil_tree_node *attr_node = NULL;
	struct cil_typeattribute *attr = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, attrtypes->attr_str, CIL_SYM_TYPES, extra_args, &attr_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	attr_node = attr_datum->nodes->head->data;

	if (attr_node->flavor != CIL_TYPEATTRIBUTE) {
		rc = SEPOL_ERR;
		cil_log(CIL_ERR, "Attribute type not an attribute\n");
		goto exit;
	}

	attr = (struct cil_typeattribute*)attr_datum;

	rc = cil_resolve_expr(CIL_TYPEATTRIBUTESET, attrtypes->str_expr, &attrtypes->datum_expr, current, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_verify_no_self_reference(attr_datum, attrtypes->datum_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (attr->expr_list == NULL) {
		cil_list_init(&attr->expr_list, CIL_TYPEATTRIBUTE);
	}

	cil_list_append(attr->expr_list, CIL_LIST, attrtypes->datum_expr);

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_aliasactual(struct cil_tree_node *current, void *extra_args, enum cil_flavor flavor)
{
	int rc = SEPOL_ERR;
	enum cil_sym_index sym_index;
	struct cil_aliasactual *aliasactual = current->data;
	struct cil_symtab_datum *alias_datum = NULL;
	struct cil_symtab_datum *actual_datum = NULL;
	struct cil_alias *alias;

	rc = cil_flavor_to_symtab_index(flavor, &sym_index);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	rc = cil_resolve_name(current, aliasactual->alias_str, sym_index, extra_args, &alias_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_resolve_name(current, aliasactual->actual_str, sym_index, extra_args, &actual_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	alias = (struct cil_alias *)alias_datum;

	if (alias->actual != NULL) {
		cil_log(CIL_ERR, "Alias cannot bind more than one value\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	alias->actual = actual_datum;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_alias_to_actual(struct cil_tree_node *current, enum cil_flavor flavor)
{
	struct cil_alias *alias = current->data;
	struct cil_alias *a1 = current->data;
	struct cil_alias *a2 = current->data;
	struct cil_tree_node *a1_node = NULL;
	int steps = 0;
	int limit = 2;

	if (alias->actual == NULL) {
		cil_log(CIL_ERR, "Alias declared but not used at line %d of %s\n",current->line, current->path);
		return SEPOL_ERR;
	}

	a1_node = a1->datum.nodes->head->data;

	while (flavor != a1_node->flavor) {
		a1 = a1->actual;
		a1_node = a1->datum.nodes->head->data;
		steps += 1;

		if (a1 == a2) {
			cil_log(CIL_ERR, "Circular alias found: %s ", a1->datum.name);
			a1 = a1->actual;
			while (a1 != a2) {
				cil_log(CIL_ERR, "%s ", a1->datum.name);
				a1 = a1->actual;
			}
			cil_log(CIL_ERR,"\n");
			return SEPOL_ERR;
		}

		if (steps == limit) {
			steps = 0;
			limit *= 2;
			a2 = a1;
		}
	}

	alias->actual = a1;

	return SEPOL_OK;
}

int cil_resolve_typepermissive(struct cil_tree_node *current, void *extra_args)
{
	struct cil_typepermissive *typeperm = current->data;
	struct cil_symtab_datum *type_datum = NULL;
	struct cil_tree_node *type_node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, typeperm->type_str, CIL_SYM_TYPES, extra_args, &type_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	type_node = type_datum->nodes->head->data;

	if (type_node->flavor != CIL_TYPE && type_node->flavor != CIL_TYPEALIAS) {
		cil_log(CIL_ERR, "Typepermissive must be a type or type alias\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	typeperm->type = type_datum;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_nametypetransition(struct cil_tree_node *current, void *extra_args)
{
	struct cil_args_resolve *args = extra_args;
	struct cil_nametypetransition *nametypetrans = current->data;
	struct cil_symtab_datum *src_datum = NULL;
	struct cil_symtab_datum *tgt_datum = NULL;
	struct cil_symtab_datum *obj_datum = NULL;
	struct cil_symtab_datum *name_datum = NULL;
	struct cil_symtab_datum *result_datum = NULL;
	struct cil_tree_node *result_node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, nametypetrans->src_str, CIL_SYM_TYPES, extra_args, &src_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	nametypetrans->src = src_datum;
	cil_type_used(src_datum);

	rc = cil_resolve_name(current, nametypetrans->tgt_str, CIL_SYM_TYPES, extra_args, &tgt_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	nametypetrans->tgt = tgt_datum;
	cil_type_used(tgt_datum);

	rc = cil_resolve_name(current, nametypetrans->obj_str, CIL_SYM_CLASSES, extra_args, &obj_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	nametypetrans->obj = (struct cil_class*)obj_datum;

	nametypetrans->name = __cil_insert_name(args->db, nametypetrans->name_str, current);
	if (nametypetrans->name == NULL) {
		rc = cil_resolve_name(current, nametypetrans->name_str, CIL_SYM_NAMES, extra_args, &name_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		nametypetrans->name = (struct cil_name *)name_datum;
	}

	rc = cil_resolve_name(current, nametypetrans->result_str, CIL_SYM_TYPES, extra_args, &result_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	result_node = result_datum->nodes->head->data;

	if (result_node->flavor != CIL_TYPE && result_node->flavor != CIL_TYPEALIAS) {
		cil_log(CIL_ERR, "typetransition result is not a type or type alias\n");
		rc = SEPOL_ERR;
		goto exit;
	}
	nametypetrans->result = result_datum;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_rangetransition(struct cil_tree_node *current, void *extra_args)
{
	struct cil_rangetransition *rangetrans = current->data;
	struct cil_symtab_datum *src_datum = NULL;
	struct cil_symtab_datum *exec_datum = NULL;
	struct cil_symtab_datum *obj_datum = NULL;
	struct cil_symtab_datum *range_datum = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, rangetrans->src_str, CIL_SYM_TYPES, extra_args, &src_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	rangetrans->src = src_datum;
	cil_type_used(src_datum);

	rc = cil_resolve_name(current, rangetrans->exec_str, CIL_SYM_TYPES, extra_args, &exec_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	rangetrans->exec = exec_datum;
	cil_type_used(exec_datum);

	rc = cil_resolve_name(current, rangetrans->obj_str, CIL_SYM_CLASSES, extra_args, &obj_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	rangetrans->obj = (struct cil_class*)obj_datum;

	if (rangetrans->range_str != NULL) {
		rc = cil_resolve_name(current, rangetrans->range_str, CIL_SYM_LEVELRANGES, extra_args, &range_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		rangetrans->range = (struct cil_levelrange*)range_datum;

		/* This could still be an anonymous levelrange even if range_str is set, if range_str is a param_str*/
		if (rangetrans->range->datum.name == NULL) {
			rc = cil_resolve_levelrange(current, rangetrans->range, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	} else {
		rc = cil_resolve_levelrange(current, rangetrans->range, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int __class_update_perm_values(__attribute__((unused)) hashtab_key_t k, hashtab_datum_t d, void *args)
{
	struct cil_perm *perm = (struct cil_perm *)d;

	perm->value += *((int *)args);

	return SEPOL_OK;
}

int cil_resolve_classcommon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_class *class = NULL;
	struct cil_class *common = NULL;
	struct cil_classcommon *clscom = current->data;
	struct cil_symtab_datum *class_datum = NULL;
	struct cil_symtab_datum *common_datum = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, clscom->class_str, CIL_SYM_CLASSES, extra_args, &class_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_resolve_name(current, clscom->common_str, CIL_SYM_COMMONS, extra_args, &common_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	class = (struct cil_class *)class_datum;
	common = (struct cil_class *)common_datum;
	if (class->common != NULL) {
		cil_log(CIL_ERR, "class cannot be associeated with more than one common\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	class->common = common;

	cil_symtab_map(&class->perms, __class_update_perm_values, &common->num_perms);

	class->num_perms += common->num_perms;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_classmapping(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_classmapping *mapping = current->data;
	struct cil_class *map = NULL;
	struct cil_perm *mp = NULL;
	struct cil_symtab_datum *datum = NULL;
	struct cil_list_item *curr;

	rc = cil_resolve_name(current, mapping->map_class_str, CIL_SYM_CLASSES, extra_args, &datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	map = (struct cil_class*)datum;

	rc = cil_symtab_get_datum(&map->perms, mapping->map_perm_str, &datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	mp = (struct cil_perm*)datum;

	rc = cil_resolve_classperms_list(current, mapping->classperms, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (mp->classperms == NULL) {
		cil_list_init(&mp->classperms, CIL_CLASSPERMS);
	}

	cil_list_for_each(curr, mapping->classperms) {
		cil_list_append(mp->classperms, curr->flavor, curr->data);
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_userrole(struct cil_tree_node *current, void *extra_args)
{
	struct cil_userrole *userrole = current->data;
	struct cil_symtab_datum *user_datum = NULL;
	struct cil_symtab_datum *role_datum = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, userrole->user_str, CIL_SYM_USERS, extra_args, &user_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	userrole->user = (struct cil_user*)user_datum;

	rc = cil_resolve_name(current, userrole->role_str, CIL_SYM_ROLES, extra_args, &role_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	userrole->role = role_datum;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_userlevel(struct cil_tree_node *current, void *extra_args)
{
	struct cil_userlevel *usrlvl = current->data;
	struct cil_symtab_datum *user_datum = NULL;
	struct cil_symtab_datum *lvl_datum = NULL;
	struct cil_user *user = NULL;
	struct cil_tree_node *user_node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, usrlvl->user_str, CIL_SYM_USERS, extra_args, &user_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	user_node = user_datum->nodes->head->data;

	if (user_node->flavor != CIL_USER) {
		cil_log(CIL_ERR, "Userlevel must be a user\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	user = (struct cil_user*)user_datum;

	if (usrlvl->level_str != NULL) {
		rc = cil_resolve_name(current, usrlvl->level_str, CIL_SYM_LEVELS, extra_args, &lvl_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		usrlvl->level = (struct cil_level*)lvl_datum;
		user->dftlevel = usrlvl->level;

		/* This could still be an anonymous level even if level_str is set, if level_str is a param_str*/
		if (user->dftlevel->datum.name == NULL) {
			rc = cil_resolve_level(current, user->dftlevel, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	} else if (usrlvl->level != NULL) {
		rc = cil_resolve_level(current, usrlvl->level, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		user->dftlevel = usrlvl->level;
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_userrange(struct cil_tree_node *current, void *extra_args)
{
	struct cil_userrange *userrange = current->data;
	struct cil_symtab_datum *user_datum = NULL;
	struct cil_symtab_datum *range_datum = NULL;
	struct cil_user *user = NULL;
	struct cil_tree_node *user_node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, userrange->user_str, CIL_SYM_USERS, extra_args, &user_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	user_node = user_datum->nodes->head->data;

	if (user_node->flavor != CIL_USER) {
		cil_log(CIL_ERR, "Userrange must be a user: %s\n", user_datum->fqn);
		rc = SEPOL_ERR;
		goto exit;
	}

	user = (struct cil_user*)user_datum;

	if (userrange->range_str != NULL) {
		rc = cil_resolve_name(current, userrange->range_str, CIL_SYM_LEVELRANGES, extra_args, &range_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		userrange->range = (struct cil_levelrange*)range_datum;
		user->range = userrange->range;

		/* This could still be an anonymous levelrange even if levelrange_str is set, if levelrange_str is a param_str*/
		if (user->range->datum.name == NULL) {
			rc = cil_resolve_levelrange(current, user->range, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	} else if (userrange->range != NULL) {
		rc = cil_resolve_levelrange(current, userrange->range, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		user->range = userrange->range;
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_userprefix(struct cil_tree_node *current, void *extra_args)
{
	struct cil_userprefix *userprefix = current->data;
	struct cil_symtab_datum *user_datum = NULL;
	struct cil_tree_node *user_node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, userprefix->user_str, CIL_SYM_USERS, extra_args, &user_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	user_node = user_datum->nodes->head->data;

	if (user_node->flavor != CIL_USER) {
		cil_log(CIL_ERR, "Userprefix must be a user: %s\n", user_datum->fqn);
		rc = SEPOL_ERR;
		goto exit;
	}

	userprefix->user = (struct cil_user*)user_datum;

exit:
	return rc;
}

int cil_resolve_selinuxuser(struct cil_tree_node *current, void *extra_args)
{
	struct cil_selinuxuser *selinuxuser = current->data;
	struct cil_symtab_datum *user_datum = NULL;
	struct cil_symtab_datum *lvlrange_datum = NULL;
	struct cil_tree_node *user_node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, selinuxuser->user_str, CIL_SYM_USERS, extra_args, &user_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	user_node = user_datum->nodes->head->data;

	if (user_node->flavor != CIL_USER) {
		cil_log(CIL_ERR, "Selinuxuser must be a user: %s\n", user_datum->fqn);
		rc = SEPOL_ERR;
		goto exit;
	}

	selinuxuser->user = (struct cil_user*)user_datum;

	if (selinuxuser->range_str != NULL) {
		rc = cil_resolve_name(current, selinuxuser->range_str, CIL_SYM_LEVELRANGES, extra_args, &lvlrange_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		selinuxuser->range = (struct cil_levelrange*)lvlrange_datum;

		/* This could still be an anonymous levelrange even if range_str is set, if range_str is a param_str*/
		if (selinuxuser->range->datum.name == NULL) {
			rc = cil_resolve_levelrange(current, selinuxuser->range, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	} else if (selinuxuser->range != NULL) {
		rc = cil_resolve_levelrange(current, selinuxuser->range, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	rc = SEPOL_OK;
exit:
	return rc;
}

int cil_resolve_roletype(struct cil_tree_node *current, void *extra_args)
{
	struct cil_roletype *roletype = current->data;
	struct cil_symtab_datum *role_datum = NULL;
	struct cil_symtab_datum *type_datum = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, roletype->role_str, CIL_SYM_ROLES, extra_args, &role_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	roletype->role = (struct cil_role*)role_datum;

	rc = cil_resolve_name(current, roletype->type_str, CIL_SYM_TYPES, extra_args, &type_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	roletype->type = (struct cil_type*)type_datum;
	cil_type_used(type_datum);

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_roletransition(struct cil_tree_node *current, void *extra_args)
{
	struct cil_roletransition *roletrans = current->data;
	struct cil_symtab_datum *src_datum = NULL;
	struct cil_symtab_datum *tgt_datum = NULL;
	struct cil_symtab_datum *obj_datum = NULL;
	struct cil_symtab_datum *result_datum = NULL;
	struct cil_tree_node *node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, roletrans->src_str, CIL_SYM_ROLES, extra_args, &src_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	roletrans->src = (struct cil_role*)src_datum;

	rc = cil_resolve_name(current, roletrans->tgt_str, CIL_SYM_TYPES, extra_args, &tgt_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	roletrans->tgt = tgt_datum;
	cil_type_used(tgt_datum);

	rc = cil_resolve_name(current, roletrans->obj_str, CIL_SYM_CLASSES, extra_args, &obj_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	roletrans->obj = (struct cil_class*)obj_datum;

	rc = cil_resolve_name(current, roletrans->result_str, CIL_SYM_ROLES, extra_args, &result_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	node = result_datum->nodes->head->data;
	if (node->flavor != CIL_ROLE) {
		rc = SEPOL_ERR;
		printf("%i\n", node->flavor);
		cil_log(CIL_ERR, "roletransition must result in a role, but %s is a %s\n", roletrans->result_str, cil_node_to_string(node));
		goto exit;
	}
	roletrans->result = (struct cil_role*)result_datum;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_roleallow(struct cil_tree_node *current, void *extra_args)
{
	struct cil_roleallow *roleallow = current->data;
	struct cil_symtab_datum *src_datum = NULL;
	struct cil_symtab_datum *tgt_datum = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, roleallow->src_str, CIL_SYM_ROLES, extra_args, &src_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	roleallow->src = (struct cil_role*)src_datum;

	rc = cil_resolve_name(current, roleallow->tgt_str, CIL_SYM_ROLES, extra_args, &tgt_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	roleallow->tgt = (struct cil_role*)tgt_datum;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_roleattributeset(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_roleattributeset *attrroles = current->data;
	struct cil_symtab_datum *attr_datum = NULL;
	struct cil_tree_node *attr_node = NULL;
	struct cil_roleattribute *attr = NULL;

	rc = cil_resolve_name(current, attrroles->attr_str, CIL_SYM_ROLES, extra_args, &attr_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	attr_node = attr_datum->nodes->head->data;

	if (attr_node->flavor != CIL_ROLEATTRIBUTE) {
		rc = SEPOL_ERR;
		cil_log(CIL_ERR, "Attribute role not an attribute\n");
		goto exit;
	}
	attr = (struct cil_roleattribute*)attr_datum;

	rc = cil_resolve_expr(CIL_ROLEATTRIBUTESET, attrroles->str_expr, &attrroles->datum_expr, current, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_verify_no_self_reference(attr_datum, attrroles->datum_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (attr->expr_list == NULL) {
		cil_list_init(&attr->expr_list, CIL_ROLEATTRIBUTE);
	}

	cil_list_append(attr->expr_list, CIL_LIST, attrroles->datum_expr);

	return SEPOL_OK;

exit:
	return rc;
}

struct cil_ordered_list {
	int merged;
	struct cil_list *list;
	struct cil_tree_node *node;
};

void __cil_ordered_list_init(struct cil_ordered_list **ordered)
{
	*ordered = cil_malloc(sizeof(**ordered));

	(*ordered)->merged = CIL_FALSE;
	(*ordered)->list = NULL;
	(*ordered)->node = NULL;
}

void __cil_ordered_list_destroy(struct cil_ordered_list **ordered)
{
	cil_list_destroy(&(*ordered)->list, CIL_FALSE);
	(*ordered)->node = NULL;
	free(*ordered);
	*ordered = NULL;
}

void __cil_ordered_lists_destroy(struct cil_list **ordered_lists)
{
	struct cil_list_item *item = NULL;

	if (ordered_lists == NULL || *ordered_lists == NULL) {
		return;
	}

	item = (*ordered_lists)->head;
	while (item != NULL) {
		struct cil_list_item *next = item->next;
		struct cil_ordered_list *ordered = item->data;
		__cil_ordered_list_destroy(&ordered);
		free(item);
		item = next;
	}
	free(*ordered_lists);
	*ordered_lists = NULL;
}

void __cil_ordered_lists_reset(struct cil_list **ordered_lists)
{
	__cil_ordered_lists_destroy(ordered_lists);
	cil_list_init(ordered_lists, CIL_LIST_ITEM);
}

struct cil_list_item *__cil_ordered_item_insert(struct cil_list *old, struct cil_list_item *curr, struct cil_list_item *item)
{
	if (item->flavor == CIL_SID) {
		struct cil_sid *sid = item->data;
		if (sid->ordered == CIL_TRUE) {
			cil_log(CIL_ERR, "SID %s has already been merged into the ordered list\n", sid->datum.name);
			return NULL;
		}
		sid->ordered = CIL_TRUE;
	} else if (item->flavor == CIL_CLASS) {
		struct cil_class *class = item->data;
		if (class->ordered == CIL_TRUE) {
			cil_log(CIL_ERR, "Class %s has already been merged into the ordered list\n", class->datum.name);
			return NULL;
		}
		class->ordered = CIL_TRUE;
	} else if (item->flavor == CIL_CAT) {
		struct cil_cat *cat = item->data;
		if (cat->ordered == CIL_TRUE) {
			cil_log(CIL_ERR, "Category %s has already been merged into the ordered list\n", cat->datum.name);
			return NULL;
		}
		cat->ordered = CIL_TRUE;
	} else if (item->flavor == CIL_SENS) {
		struct cil_sens *sens = item->data;
		if (sens->ordered == CIL_TRUE) {
			cil_log(CIL_ERR, "Sensitivity %s has already been merged into the ordered list\n", sens->datum.name);
			return NULL;
		}
		sens->ordered = CIL_TRUE;
	}

	return cil_list_insert(old, curr, item->flavor, item->data);
}

int __cil_ordered_list_insert(struct cil_list *old, struct cil_list_item *ocurr, struct cil_list_item *nstart, struct cil_list_item *nstop)
{
	struct cil_list_item *ncurr = NULL;

	for (ncurr = nstart; ncurr != nstop; ncurr = ncurr->next) {
		ocurr = __cil_ordered_item_insert(old, ocurr, ncurr);
		if (ocurr == NULL) {
			return SEPOL_ERR;
		}
	}
	return SEPOL_OK;
}

struct cil_list_item *__cil_ordered_find_match(struct cil_list_item *t, struct cil_list_item *i)
{
	while (i) {
		if (i->data == t->data) {
			return i;
		}
		i = i->next;
	}
	return NULL;
}

int __cil_ordered_lists_merge(struct cil_list *old, struct cil_list *new)
{
	struct cil_list_item *omatch = NULL;
	struct cil_list_item *ofirst = old->head;
	struct cil_list_item *ocurr = NULL;
	struct cil_list_item *oprev = NULL;
	struct cil_list_item *nmatch = NULL;
	struct cil_list_item *nfirst = new->head;
	struct cil_list_item *ncurr = NULL;
	int rc = SEPOL_ERR;

	if (nfirst == NULL) {
		return SEPOL_OK;
	}

	if (ofirst == NULL) {
		/* First list added */
		rc = __cil_ordered_list_insert(old, NULL, nfirst, NULL);
		return rc;
	}

	/* Find a match between the new list and the old one */
	for (nmatch = nfirst; nmatch; nmatch = nmatch->next) {
		omatch = __cil_ordered_find_match(nmatch, ofirst);
		if (omatch) {
			break;
		}
	}

	if (!nmatch) {
		/* List cannot be merged yet */
		return SEPOL_ERR;
	}

	if (nmatch != nfirst && omatch != ofirst) {
		/* Potential ordering conflict--try again later */
		return SEPOL_ERR;
	}

	if (nmatch != nfirst) {
		/* Prepend the beginning of the new list up to the first match to the old list */
		rc = __cil_ordered_list_insert(old, NULL, nfirst, nmatch);
		if (rc != SEPOL_OK) {
			return rc;
		}
	}

	/* In the overlapping protion, add items from the new list not in the old list */
	ncurr = nmatch->next;
	ocurr = omatch->next;
	oprev = omatch;
	while (ncurr && ocurr) {
		if (ncurr->data == ocurr->data) {
			oprev = ocurr;
			ocurr = ocurr->next;
			ncurr = ncurr->next;
		} else {
			/* Handle gap in old: old = (A C)  new = (A B C) */
			nmatch = __cil_ordered_find_match(ocurr, ncurr->next);
			if (nmatch) {
				rc = __cil_ordered_list_insert(old, oprev, ncurr, nmatch);
				if (rc != SEPOL_OK) {
					return rc;
				}
				oprev = ocurr;
				ocurr = ocurr->next;
				ncurr = nmatch->next;
				continue;
			}
			/* Handle gap in new: old = (A B C)  new = (A C) */
			omatch = __cil_ordered_find_match(ncurr, ocurr->next);
			if (omatch) {
				/* Nothing to insert, just skip */
				oprev = omatch;
				ocurr = omatch->next;
				ncurr = ncurr->next;
				continue;
			} else {
				return SEPOL_ERR;
			}
		}
	}

	if (ncurr) {
		/* Add the rest of the items from the new list */
		rc = __cil_ordered_list_insert(old, old->tail, ncurr, NULL);
		if (rc != SEPOL_OK) {
			return rc;
		}
	}

	return SEPOL_OK;
}

static int insert_unordered(struct cil_list *merged, struct cil_list *unordered)
{
	struct cil_list_item *curr = NULL;
	struct cil_ordered_list *unordered_list = NULL;
	struct cil_list_item *item = NULL;
	struct cil_list_item *ret = NULL;
	int rc = SEPOL_ERR;

	cil_list_for_each(curr, unordered) {
		unordered_list = curr->data;

		cil_list_for_each(item, unordered_list->list) {
			if (cil_list_contains(merged, item->data)) {
				/* item was declared in an ordered statement, which supercedes
				 * all unordered statements */
				if (item->flavor == CIL_CLASS) {
					cil_log(CIL_WARN, "Ignoring '%s' as it has already been declared in classorder.\n", ((struct cil_class*)(item->data))->datum.name);
				}
				continue;
			}

			ret = __cil_ordered_item_insert(merged, merged->tail, item);
			if (ret == NULL) {
				rc = SEPOL_ERR;
				goto exit;
			}
		}
	}

	rc = SEPOL_OK;

exit:
	return rc;
}

struct cil_list *__cil_ordered_lists_merge_all(struct cil_list **ordered_lists, struct cil_list **unordered_lists)
{
	struct cil_list *composite = NULL;
	struct cil_list_item *curr = NULL;
	int changed = CIL_TRUE;
	int waiting = 1;
	int rc = SEPOL_ERR;

	cil_list_init(&composite, CIL_LIST_ITEM);

	while (waiting && changed == CIL_TRUE) {
		changed = CIL_FALSE;
		waiting = 0;
		cil_list_for_each(curr, *ordered_lists) {
			struct cil_ordered_list *ordered_list = curr->data;
			if (ordered_list->merged == CIL_FALSE) {
				rc = __cil_ordered_lists_merge(composite, ordered_list->list);
				if (rc != SEPOL_OK) {
					/* Can't merge yet */
					waiting++;
				} else {
					ordered_list->merged = CIL_TRUE;
					changed = CIL_TRUE;
				}
			}
		}
		if (waiting > 0 && changed == CIL_FALSE) {
			cil_list_for_each(curr, *ordered_lists) {
				struct cil_ordered_list *ordered_list = curr->data;
				if (ordered_list->merged == CIL_FALSE) {
					cil_log(CIL_ERR, "Unable to merge ordered list at line %d of %s\n",ordered_list->node->line, ordered_list->node->path);
				}
			}
			goto exit;
		}
	}

	if (unordered_lists != NULL) {
		rc = insert_unordered(composite, *unordered_lists);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	__cil_ordered_lists_destroy(ordered_lists);
	__cil_ordered_lists_destroy(unordered_lists);

	return composite;

exit:
	__cil_ordered_lists_destroy(ordered_lists);
	__cil_ordered_lists_destroy(unordered_lists);
	cil_list_destroy(&composite, CIL_FALSE);
	return NULL;
}

int cil_resolve_classorder(struct cil_tree_node *current, void *extra_args)
{
	struct cil_args_resolve *args = extra_args;
	struct cil_list *classorder_list = args->classorder_lists;
	struct cil_list *unordered_classorder_list = args->unordered_classorder_lists;
	struct cil_classorder *classorder = current->data;
	struct cil_list *new = NULL;
	struct cil_list_item *curr = NULL;
	struct cil_symtab_datum *datum = NULL;
	struct cil_ordered_list *class_list = NULL;
	int rc = SEPOL_ERR;
	int unordered = CIL_FALSE;

	cil_list_init(&new, CIL_CLASSORDER);

	cil_list_for_each(curr, classorder->class_list_str) {
		if (curr->data == CIL_KEY_UNORDERED) {
			unordered = CIL_TRUE;
			continue;
		}

		rc = cil_resolve_name(current, (char *)curr->data, CIL_SYM_CLASSES, extra_args, &datum);
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Failed to resolve class %s in classorder\n", (char *)curr->data);
			goto exit;
		}
		cil_list_append(new, CIL_CLASS, datum);
	}

	__cil_ordered_list_init(&class_list);
	class_list->list = new;
	class_list->node = current;
	if (unordered) {
		cil_list_append(unordered_classorder_list, CIL_CLASSORDER, class_list);
	} else {
		cil_list_append(classorder_list, CIL_CLASSORDER, class_list);
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_sidorder(struct cil_tree_node *current, void *extra_args)
{
	struct cil_args_resolve *args = extra_args;
	struct cil_list *sidorder_list = args->sidorder_lists;
	struct cil_sidorder *sidorder = current->data;
	struct cil_list *new = NULL;
	struct cil_list_item *curr = NULL;
	struct cil_symtab_datum *datum = NULL;
	struct cil_ordered_list *ordered = NULL;
	int rc = SEPOL_ERR;

	cil_list_init(&new, CIL_SIDORDER);

	cil_list_for_each(curr, sidorder->sid_list_str) {
		rc = cil_resolve_name(current, (char *)curr->data, CIL_SYM_SIDS, extra_args, &datum);
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Failed to resolve sid %s in sidorder\n", (char *)curr->data);
			goto exit;
		}
		cil_list_append(new, CIL_SID, datum);
	}

	__cil_ordered_list_init(&ordered);
	ordered->list = new;
	ordered->node = current;
	cil_list_append(sidorder_list, CIL_SIDORDER, ordered);

	return SEPOL_OK;

exit:
	return rc;
}

void cil_set_cat_values(struct cil_list *ordered_cats, struct cil_db *db)
{
	struct cil_list_item *curr;
	int v = 0;

	cil_list_for_each(curr, ordered_cats) {
		struct cil_cat *cat = curr->data;
		cat->value = v;
		v++;
	}

	db->num_cats = v;
}

int cil_resolve_catorder(struct cil_tree_node *current, void *extra_args)
{
	struct cil_args_resolve *args = extra_args;
	struct cil_list *catorder_list = args->catorder_lists;
	struct cil_catorder *catorder = current->data;
	struct cil_list *new = NULL;
	struct cil_list_item *curr = NULL;
	struct cil_symtab_datum *cat_datum;
	struct cil_cat *cat = NULL;
	struct cil_ordered_list *ordered = NULL;
	int rc = SEPOL_ERR;

	cil_list_init(&new, CIL_CATORDER);

	cil_list_for_each(curr, catorder->cat_list_str) {
		struct cil_tree_node *node = NULL;
		rc = cil_resolve_name(current, (char *)curr->data, CIL_SYM_CATS, extra_args, &cat_datum);
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Failed to resolve category %s in categoryorder\n", (char *)curr->data);
			goto exit;
		}
		node = cat_datum->nodes->head->data;
		if (node->flavor != CIL_CAT) {
			cil_log(CIL_ERR, "%s is not a category. Only categories are allowed in categoryorder statements\n", cat_datum->name);
			rc = SEPOL_ERR;
			goto exit;
		}
		cat = (struct cil_cat *)cat_datum;
		cil_list_append(new, CIL_CAT, cat);
	}

	__cil_ordered_list_init(&ordered);
	ordered->list = new;
	ordered->node = current;
	cil_list_append(catorder_list, CIL_CATORDER, ordered);

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_sensitivityorder(struct cil_tree_node *current, void *extra_args)
{
	struct cil_args_resolve *args = extra_args;
	struct cil_list *sensitivityorder_list = args->sensitivityorder_lists;
	struct cil_sensorder *sensorder = current->data;
	struct cil_list *new = NULL;
	struct cil_list_item *curr = NULL;
	struct cil_symtab_datum *datum = NULL;
	struct cil_ordered_list *ordered = NULL;
	int rc = SEPOL_ERR;

	cil_list_init(&new, CIL_LIST_ITEM);

	cil_list_for_each(curr, sensorder->sens_list_str) {
		rc = cil_resolve_name(current, (char *)curr->data, CIL_SYM_SENS, extra_args, &datum);
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Failed to resolve sensitivty %s in sensitivityorder\n", (char *)curr->data);
			goto exit;
		}
		cil_list_append(new, CIL_SENS, datum);
	}

	__cil_ordered_list_init(&ordered);
	ordered->list = new;
	ordered->node = current;
	cil_list_append(sensitivityorder_list, CIL_SENSITIVITYORDER, ordered);

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_cats(struct cil_tree_node *current, struct cil_cats *cats, void *extra_args)
{
	int rc = SEPOL_ERR;

	rc = cil_resolve_expr(CIL_CATSET, cats->str_expr, &cats->datum_expr, current, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	
	return SEPOL_OK;

exit:
	return rc;
}


int cil_resolve_catset(struct cil_tree_node *current, struct cil_catset *catset, void *extra_args)
{
	int rc = SEPOL_ERR;

	rc = cil_resolve_cats(current, catset->cats, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_verify_no_self_reference((struct cil_symtab_datum *)catset, catset->cats->datum_expr);
	if (rc != SEPOL_OK) {
		cil_list_destroy(&catset->cats->datum_expr, CIL_FALSE);
		goto exit;
	}

exit:
	return rc;
}

int cil_resolve_senscat(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_senscat *senscat = current->data;
	struct cil_symtab_datum *sens_datum;
	struct cil_sens *sens = NULL;

	rc = cil_resolve_name(current, (char*)senscat->sens_str, CIL_SYM_SENS, extra_args, &sens_datum);
	if (rc != SEPOL_OK) {
		cil_log(CIL_ERR, "Failed to find sensitivity\n");
		goto exit;
	}

	rc = cil_resolve_cats(current, senscat->cats, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	sens = (struct cil_sens *)sens_datum;

	if (sens->cats_list == NULL ) {
		cil_list_init(&sens->cats_list, CIL_CAT);
	}

	cil_list_append(sens->cats_list, CIL_CAT, senscat->cats);

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_level(struct cil_tree_node *current, struct cil_level *level, void *extra_args)
{
	struct cil_symtab_datum *sens_datum = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, (char*)level->sens_str, CIL_SYM_SENS, extra_args, &sens_datum);
	if (rc != SEPOL_OK) {
		cil_log(CIL_ERR, "Failed to find sensitivity\n");
		goto exit;
	}

	level->sens = (struct cil_sens *)sens_datum;

	if (level->cats != NULL) {
		rc = cil_resolve_cats(current, level->cats, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_levelrange(struct cil_tree_node *current, struct cil_levelrange *lvlrange, void *extra_args)
{
	struct cil_symtab_datum *low_datum = NULL;
	struct cil_symtab_datum *high_datum = NULL;
	int rc = SEPOL_ERR;

	if (lvlrange->low_str != NULL) {
		rc = cil_resolve_name(current, lvlrange->low_str, CIL_SYM_LEVELS, extra_args, &low_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		lvlrange->low = (struct cil_level*)low_datum;

		/* This could still be an anonymous level even if low_str is set, if low_str is a param_str */
		if (lvlrange->low->datum.name == NULL) {
			rc = cil_resolve_level(current, lvlrange->low, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	} else if (lvlrange->low != NULL) {
		rc = cil_resolve_level(current, lvlrange->low, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	if (lvlrange->high_str != NULL) {
		rc = cil_resolve_name(current, lvlrange->high_str, CIL_SYM_LEVELS, extra_args, &high_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		lvlrange->high = (struct cil_level*)high_datum;

		/* This could still be an anonymous level even if high_str is set, if high_str is a param_str */
		if (lvlrange->high->datum.name == NULL) {
			rc = cil_resolve_level(current, lvlrange->high, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	} else if (lvlrange->high != NULL) {
		rc = cil_resolve_level(current, lvlrange->high, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_constrain(struct cil_tree_node *current, void *extra_args)
{
	struct cil_constrain *cons = current->data;
	int rc = SEPOL_ERR;

	rc = cil_resolve_classperms_list(current, cons->classperms, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_resolve_expr(CIL_CONSTRAIN, cons->str_expr, &cons->datum_expr, current, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_validatetrans(struct cil_tree_node *current, void *extra_args)
{
	struct cil_validatetrans *validtrans = current->data;
	struct cil_args_resolve *args = extra_args;
	struct cil_symtab_datum *class_datum = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, validtrans->class_str, CIL_SYM_CLASSES, args, &class_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	validtrans->class = (struct cil_class*)class_datum;

	rc = cil_resolve_expr(CIL_VALIDATETRANS, validtrans->str_expr, &validtrans->datum_expr, current, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_context(struct cil_tree_node *current, struct cil_context *context, void *extra_args)
{
	struct cil_symtab_datum *user_datum = NULL;
	struct cil_symtab_datum *role_datum = NULL;
	struct cil_symtab_datum *type_datum = NULL;
	struct cil_tree_node *node = NULL;
	struct cil_symtab_datum *lvlrange_datum = NULL;

	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, context->user_str, CIL_SYM_USERS, extra_args, &user_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	node = user_datum->nodes->head->data;

	if (node->flavor != CIL_USER) {
		cil_log(CIL_ERR, "Context user must be a user: %s\n", user_datum->fqn);
		rc = SEPOL_ERR;
		goto exit;
	}

	context->user = (struct cil_user*)user_datum;

	rc = cil_resolve_name(current, context->role_str, CIL_SYM_ROLES, extra_args, &role_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	node = role_datum->nodes->head->data;
	if (node->flavor != CIL_ROLE) {
		rc = SEPOL_ERR;
		cil_log(CIL_ERR, "Context role not a role: %s\n", role_datum->fqn);
		goto exit;
	}

	context->role = (struct cil_role*)role_datum;

	rc = cil_resolve_name(current, context->type_str, CIL_SYM_TYPES, extra_args, &type_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	node = type_datum->nodes->head->data;

	if (node->flavor != CIL_TYPE && node->flavor != CIL_TYPEALIAS) {
		rc = SEPOL_ERR;
		cil_log(CIL_ERR, "Type not a type or type alias\n");
		goto exit;
	}
	context->type = type_datum;

	if (context->range_str != NULL) {
		rc = cil_resolve_name(current, context->range_str, CIL_SYM_LEVELRANGES, extra_args, &lvlrange_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		context->range = (struct cil_levelrange*)lvlrange_datum;

		/* This could still be an anonymous levelrange even if levelrange_str is set, if levelrange_str is a param_str*/
		if (context->range->datum.name == NULL) {
			rc = cil_resolve_levelrange(current, context->range, extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	} else if (context->range != NULL) {
		rc = cil_resolve_levelrange(current, context->range, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_filecon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_filecon *filecon = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (filecon->context_str != NULL) {
		rc = cil_resolve_name(current, filecon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			return rc;
		}
		filecon->context = (struct cil_context*)context_datum;
	} else if (filecon->context != NULL) {
		rc = cil_resolve_context(current, filecon->context, extra_args);
		if (rc != SEPOL_OK) {
			return rc;
		}
	}

	return SEPOL_OK;
}

int cil_resolve_portcon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_portcon *portcon = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (portcon->context_str != NULL) {
		rc = cil_resolve_name(current, portcon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		portcon->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, portcon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_genfscon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_genfscon *genfscon = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (genfscon->context_str != NULL) {
		rc = cil_resolve_name(current, genfscon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		genfscon->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, genfscon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_nodecon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_nodecon *nodecon = current->data;
	struct cil_symtab_datum *addr_datum = NULL;
	struct cil_symtab_datum *mask_datum = NULL;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (nodecon->addr_str != NULL) {
		rc = cil_resolve_name(current, nodecon->addr_str, CIL_SYM_IPADDRS, extra_args, &addr_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		nodecon->addr = (struct cil_ipaddr*)addr_datum;
	}

	if (nodecon->mask_str != NULL) {
		rc = cil_resolve_name(current, nodecon->mask_str, CIL_SYM_IPADDRS, extra_args, &mask_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		nodecon->mask = (struct cil_ipaddr*)mask_datum;
	}

	if (nodecon->context_str != NULL) {
		rc = cil_resolve_name(current, nodecon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		nodecon->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, nodecon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	if (nodecon->addr->family != nodecon->mask->family) {
		cil_log(CIL_ERR, "Nodecon ip address not in the same family\n");
		rc = SEPOL_ERR;
		goto exit;
	}


	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_netifcon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_netifcon *netifcon = current->data;
	struct cil_symtab_datum *ifcon_datum = NULL;
	struct cil_symtab_datum *packcon_datum = NULL;

	int rc = SEPOL_ERR;

	if (netifcon->if_context_str != NULL) {
		rc = cil_resolve_name(current, netifcon->if_context_str, CIL_SYM_CONTEXTS, extra_args, &ifcon_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		netifcon->if_context = (struct cil_context*)ifcon_datum;
	} else {
		rc = cil_resolve_context(current, netifcon->if_context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	if (netifcon->packet_context_str != NULL) {
		rc = cil_resolve_name(current, netifcon->packet_context_str, CIL_SYM_CONTEXTS, extra_args, &packcon_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		netifcon->packet_context = (struct cil_context*)packcon_datum;
	} else {
		rc = cil_resolve_context(current, netifcon->packet_context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}
	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_pirqcon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_pirqcon *pirqcon = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (pirqcon->context_str != NULL) {
		rc = cil_resolve_name(current, pirqcon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		pirqcon->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, pirqcon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_iomemcon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_iomemcon *iomemcon = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (iomemcon->context_str != NULL) {
		rc = cil_resolve_name(current, iomemcon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		iomemcon->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, iomemcon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_ioportcon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_ioportcon *ioportcon = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (ioportcon->context_str != NULL) {
		rc = cil_resolve_name(current, ioportcon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		ioportcon->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, ioportcon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_pcidevicecon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_pcidevicecon *pcidevicecon = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (pcidevicecon->context_str != NULL) {
		rc = cil_resolve_name(current, pcidevicecon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		pcidevicecon->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, pcidevicecon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_devicetreecon(struct cil_tree_node *current, void *extra_args)
{
	struct cil_devicetreecon *devicetreecon = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (devicetreecon->context_str != NULL) {
		rc = cil_resolve_name(current, devicetreecon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		devicetreecon->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, devicetreecon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_fsuse(struct cil_tree_node *current, void *extra_args)
{
	struct cil_fsuse *fsuse = current->data;
	struct cil_symtab_datum *context_datum = NULL;
	int rc = SEPOL_ERR;

	if (fsuse->context_str != NULL) {
		rc = cil_resolve_name(current, fsuse->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		fsuse->context = (struct cil_context*)context_datum;
	} else {
		rc = cil_resolve_context(current, fsuse->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_sidcontext(struct cil_tree_node *current, void *extra_args)
{
	struct cil_sidcontext *sidcon = current->data;
	struct cil_symtab_datum *sid_datum = NULL;
	struct cil_symtab_datum *context_datum = NULL;
	struct cil_sid *sid = NULL;

	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, sidcon->sid_str, CIL_SYM_SIDS, extra_args, &sid_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	sid = (struct cil_sid*)sid_datum;

	if (sidcon->context_str != NULL) {
		rc = cil_resolve_name(current, sidcon->context_str, CIL_SYM_CONTEXTS, extra_args, &context_datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		sidcon->context = (struct cil_context*)context_datum;
	} else if (sidcon->context != NULL) {
		rc = cil_resolve_context(current, sidcon->context, extra_args);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	if (sid->context != NULL) {
		cil_log(CIL_ERR, "sid's cannot be associated with more than one context\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	sid->context = sidcon->context;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_blockinherit_link(struct cil_tree_node *current, void *extra_args)
{
	struct cil_blockinherit *inherit = current->data;
	struct cil_symtab_datum *block_datum = NULL;
	struct cil_tree_node *node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, inherit->block_str, CIL_SYM_BLOCKS, extra_args, &block_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	node = block_datum->nodes->head->data;

	if (node->flavor != CIL_BLOCK) {
		cil_log(CIL_ERR, "%s is not a block\n", cil_node_to_string(node));
		rc = SEPOL_ERR;
		goto exit;
	}

	inherit->block = (struct cil_block *)block_datum;

	if (inherit->block->bi_nodes == NULL) {
		cil_list_init(&inherit->block->bi_nodes, CIL_NODE);
	}
	cil_list_append(inherit->block->bi_nodes, CIL_NODE, current);

	return SEPOL_OK;

exit:
	return rc;
}

void cil_print_recursive_blockinherit(struct cil_tree_node *bi_node, struct cil_tree_node *terminating_node)
{
	struct cil_list *trace = NULL;
	struct cil_list_item *item = NULL;
	struct cil_tree_node *curr = NULL;

	cil_list_init(&trace, CIL_NODE);

	for (curr = bi_node; curr != terminating_node; curr = curr->parent) {
		if (curr->flavor == CIL_BLOCK) {
			cil_list_prepend(trace, CIL_NODE, curr);
		} else {
			if (curr != bi_node) {
				cil_list_prepend(trace, CIL_NODE, NODE(((struct cil_blockinherit *)curr->data)->block));
			}
			cil_list_prepend(trace, CIL_NODE, curr);
		}
	}
	cil_list_prepend(trace, CIL_NODE, terminating_node);

	cil_list_for_each(item, trace) {
		curr = item->data;
		cil_log(CIL_ERR, "  %s:%d: ", curr->path, curr->line);

		if (curr->flavor == CIL_BLOCK) {
			cil_log(CIL_ERR, "block %s\n", DATUM(curr->data)->name);
		} else {
			cil_log(CIL_ERR, "blockinherit %s\n", ((struct cil_blockinherit *)curr->data)->block_str);
		}
	}

	cil_list_destroy(&trace, CIL_FALSE);
}

int cil_check_recursive_blockinherit(struct cil_tree_node *bi_node)
{
	struct cil_tree_node *curr = NULL;
	struct cil_blockinherit *bi = NULL;
	struct cil_block *block = NULL;
	int rc = SEPOL_ERR;

	bi = bi_node->data;

	for (curr = bi_node->parent; curr != NULL; curr = curr->parent) {
		if (curr->flavor != CIL_BLOCK) {
			continue;
		}

		block = curr->data;

		if (block != bi->block) {
			continue;
		}

		cil_log(CIL_ERR, "Recursive blockinherit found:\n");
		cil_print_recursive_blockinherit(bi_node, curr);

		rc = SEPOL_ERR;
		goto exit;
	}

	rc = SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_blockinherit_copy(struct cil_tree_node *current, void *extra_args)
{
	struct cil_block *block = current->data;
	struct cil_args_resolve *args = extra_args;
	struct cil_db *db = NULL;
	struct cil_list_item *item = NULL;
	int rc = SEPOL_ERR;

	// This block is not inherited
	if (block->bi_nodes == NULL) {
		rc = SEPOL_OK;
		goto exit;
	}

	db = args->db;

	// Make sure this is the original block and not a merged block from a blockinherit
	if (current != block->datum.nodes->head->data) {
		rc = SEPOL_OK;
		goto exit;
	}

	cil_list_for_each(item, block->bi_nodes) {
		rc = cil_check_recursive_blockinherit(item->data);
		if (rc != SEPOL_OK) {
			goto exit;
		}

		rc = cil_copy_ast(db, current, item->data);
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Failed to copy block contents into blockinherit\n");
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_blockabstract(struct cil_tree_node *current, void *extra_args)
{
	struct cil_blockabstract *abstract = current->data;
	struct cil_symtab_datum *block_datum = NULL;
	struct cil_tree_node *block_node = NULL;
	int rc = SEPOL_ERR;

	rc = cil_resolve_name(current, abstract->block_str, CIL_SYM_BLOCKS, extra_args, &block_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	block_node = block_datum->nodes->head->data;
	if (block_node->flavor != CIL_BLOCK) {
		cil_log(CIL_ERR, "Failed to resolve blockabstract to a block, rc: %d\n", rc);
		goto exit;
	}

	((struct cil_block*)block_datum)->is_abstract = CIL_TRUE;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_in(struct cil_tree_node *current, void *extra_args)
{
	struct cil_in *in = current->data;
	struct cil_args_resolve *args = extra_args;
	struct cil_db *db = NULL;
	struct cil_symtab_datum *block_datum = NULL;
	struct cil_tree_node *block_node = NULL;
	int rc = SEPOL_ERR;

	if (args != NULL) {
		db = args->db;
	}

	rc = cil_resolve_name(current, in->block_str, CIL_SYM_BLOCKS, extra_args, &block_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	block_node = block_datum->nodes->head->data;

	rc = cil_copy_ast(db, current, block_node);
	if (rc != SEPOL_OK) {
		printf("Failed to copy in, rc: %d\n", rc);
		goto exit;
	}

	cil_tree_children_destroy(current);
	current->cl_head = NULL;
	current->cl_tail = NULL;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_in_list(void *extra_args)
{
	struct cil_args_resolve *args = extra_args;
	struct cil_list *ins = args->in_list;
	struct cil_list_item *curr = NULL;
	struct cil_tree_node *node = NULL;
	struct cil_tree_node *last_failed_node = NULL;
	struct cil_in *in = NULL;
	struct cil_symtab_datum *block_datum = NULL;
	int resolved = 0;
	int unresolved = 0;
	int rc = SEPOL_ERR;

	do {
		resolved = 0;
		unresolved = 0;

		cil_list_for_each(curr, ins) {
			if (curr->flavor != CIL_NODE) {
				continue;
			}

			node = curr->data;
			in = node->data;

			rc = cil_resolve_name(node, in->block_str, CIL_SYM_BLOCKS, extra_args, &block_datum);
			if (rc != SEPOL_OK) {
				unresolved++;
				last_failed_node = node;
			} else {
				rc = cil_resolve_in(node, extra_args);
				if (rc != SEPOL_OK) {
					goto exit;
				}
				
				resolved++;
				curr->data = NULL;
				curr->flavor = CIL_NONE;
			}
		}

		if (unresolved > 0 && resolved == 0) {
			cil_log(CIL_ERR, "Failed to resolve in-statement on line %d of %s\n", last_failed_node->line, last_failed_node->path);
			rc = SEPOL_ERR;
			goto exit;
		}

	} while (unresolved > 0);

	rc = SEPOL_OK;

exit:
	return rc;
}


int cil_resolve_bounds(struct cil_tree_node *current, void *extra_args, enum cil_flavor flavor)
{
	int rc = SEPOL_ERR;
	struct cil_bounds *bounds = current->data;
	enum cil_sym_index index;
	struct cil_symtab_datum *parent_datum = NULL;
	struct cil_symtab_datum *child_datum = NULL;

	rc = cil_flavor_to_symtab_index(flavor, &index);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_resolve_name(current, bounds->parent_str, index, extra_args, &parent_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_resolve_name(current, bounds->child_str, index, extra_args, &child_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	switch (flavor) {
	case CIL_USER: {
		struct cil_user *user = (struct cil_user *)child_datum;

		if (user->bounds != NULL) {
			struct cil_tree_node *node = user->bounds->datum.nodes->head->data;
			cil_log(CIL_ERR, "User %s already bound by parent at line %u of %s\n", bounds->child_str, node->line, node->path);
			rc = SEPOL_ERR;
			goto exit;
		}

		user->bounds = (struct cil_user *)parent_datum;
		break;
	}
	case CIL_ROLE: {
		struct cil_role *role = (struct cil_role *)child_datum;

		if (role->bounds != NULL) {
			struct cil_tree_node *node = role->bounds->datum.nodes->head->data;
			cil_log(CIL_ERR, "Role %s already bound by parent at line %u of %s\n", bounds->child_str, node->line, node->path);
			rc = SEPOL_ERR;
			goto exit;
		}

		role->bounds = (struct cil_role *)parent_datum;
		break;
	}
	case CIL_TYPE: {
		struct cil_type *type = (struct cil_type *)child_datum;
		struct cil_tree_node *node = NULL;

		if (type->bounds != NULL) {
			node = ((struct cil_symtab_datum *)type->bounds)->nodes->head->data;
			cil_log(CIL_ERR, "Type %s already bound by parent at line %u of %s\n", bounds->child_str, node->line, node->path);
			cil_log(CIL_ERR, "Now being bound to parent %s at line %u of %s\n", bounds->parent_str, current->line, current->path);
			rc = SEPOL_ERR;
			goto exit;
		}

		node = parent_datum->nodes->head->data;
		if (node->flavor == CIL_TYPEATTRIBUTE) {
			cil_log(CIL_ERR, "Bounds parent %s is an attribute\n", bounds->parent_str);
			rc = SEPOL_ERR;
			goto exit;
		}

		node = child_datum->nodes->head->data;
		if (node->flavor == CIL_TYPEATTRIBUTE) {
			cil_log(CIL_ERR, "Bounds child %s is an attribute\n", bounds->child_str);
			rc = SEPOL_ERR;
			goto exit;
		}

		type->bounds = (struct cil_type *)parent_datum;
		break;
	}
	default:
		break;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad bounds statement at line %u of %s\n", current->line, current->path);
	return rc;
}

int cil_resolve_default(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_default *def = current->data;
	struct cil_list_item *curr;
	struct cil_symtab_datum *datum;

	cil_list_init(&def->class_datums, def->flavor);

	cil_list_for_each(curr, def->class_strs) {
		rc = cil_resolve_name(current, (char *)curr->data, CIL_SYM_CLASSES, extra_args, &datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		cil_list_append(def->class_datums, CIL_CLASS, datum);
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_defaultrange(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_defaultrange *def = current->data;
	struct cil_list_item *curr;
	struct cil_symtab_datum *datum;

	cil_list_init(&def->class_datums, CIL_DEFAULTRANGE);

	cil_list_for_each(curr, def->class_strs) {
		rc = cil_resolve_name(current, (char *)curr->data, CIL_SYM_CLASSES, extra_args, &datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		cil_list_append(def->class_datums, CIL_CLASS, datum);
	}

	return SEPOL_OK;

exit:
	return rc;
}

void cil_print_recursive_call(struct cil_tree_node *call_node, struct cil_tree_node *terminating_node)
{
	struct cil_list *trace = NULL;
	struct cil_list_item * item = NULL;
	struct cil_tree_node *curr = NULL;

	cil_list_init(&trace, CIL_NODE);

	for (curr = call_node; curr != terminating_node; curr = curr->parent) {
		if (curr->flavor == CIL_CALL) {
			if (curr != call_node) {
				cil_list_prepend(trace, CIL_NODE, NODE(((struct cil_call *)curr->data)->macro));
			}
			cil_list_prepend(trace, CIL_NODE, curr);
		}
	}

	if (terminating_node->flavor == CIL_MACRO) {
		cil_list_prepend(trace, CIL_NODE, terminating_node);
	} else {
		cil_list_prepend(trace, CIL_NODE, NODE(((struct cil_call *)terminating_node->data)->macro));
	}

	cil_list_for_each(item, trace) {
		curr = item->data;
		cil_log(CIL_ERR, "  %s:%d: ", curr->path, curr->line);

		if (curr->flavor == CIL_MACRO) {
			cil_log(CIL_ERR, "macro %s\n", DATUM(curr->data)->name);
		} else {
			cil_log(CIL_ERR, "call %s\n", ((struct cil_call *)curr->data)->macro_str);
		}
	}

	cil_list_destroy(&trace, CIL_FALSE);
}

int cil_check_recursive_call(struct cil_tree_node *call_node, struct cil_tree_node *macro_node)
{
	struct cil_tree_node *curr = NULL;
	struct cil_call * call = NULL;
	int rc = SEPOL_ERR;

	for (curr = call_node; curr != NULL; curr = curr->parent) {
		if (curr->flavor == CIL_CALL) {
			if (curr == call_node) {
				continue;
			}

			call = curr->data;
			if (call->macro != macro_node->data) {
				continue;
			}
		} else if (curr->flavor == CIL_MACRO) {
			if (curr != macro_node) {
				rc = SEPOL_OK;
				goto exit;
			}
		} else {
			continue;
		}

		cil_log(CIL_ERR, "Recursive macro call found:\n");
		cil_print_recursive_call(call_node, curr);

		rc = SEPOL_ERR;
		goto exit;
	}

	rc = SEPOL_OK;
exit:
	return rc;
}

int cil_resolve_call1(struct cil_tree_node *current, void *extra_args)
{
	struct cil_call *new_call = current->data;
	struct cil_args_resolve *args = extra_args;
	struct cil_db *db = NULL;
	struct cil_tree_node *macro_node = NULL;
	struct cil_symtab_datum *macro_datum = NULL;
	int rc = SEPOL_ERR;

	if (args != NULL) {
		db = args->db;
	}

	rc = cil_resolve_name(current, new_call->macro_str, CIL_SYM_BLOCKS, extra_args, &macro_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	macro_node = macro_datum->nodes->head->data;

	if (macro_node->flavor != CIL_MACRO) {
		printf("Failed to resolve %s to a macro\n", new_call->macro_str);
		rc = SEPOL_ERR;
		goto exit;
	}
	new_call->macro = (struct cil_macro*)macro_datum;

	if (new_call->macro->params != NULL ) {

		struct cil_list_item *item;
		struct cil_args *new_arg = NULL;
		struct cil_tree_node *pc = NULL;

		if (new_call->args_tree == NULL) {
			cil_log(CIL_ERR, "Missing arguments (%s, line: %d)\n", current->path, current->line);
			rc = SEPOL_ERR;
			goto exit;
		}

		pc = new_call->args_tree->root->cl_head;

		cil_list_init(&new_call->args, CIL_LIST_ITEM);

		cil_list_for_each(item, new_call->macro->params) {
			enum cil_flavor flavor = ((struct cil_param*)item->data)->flavor;

			if (pc == NULL) {
				cil_log(CIL_ERR, "Missing arguments (%s, line: %d)\n", current->path, current->line);
				rc = SEPOL_ERR;
				goto exit;
			}
			if (item->flavor != CIL_PARAM) {
				rc = SEPOL_ERR;
				goto exit;
			}

			cil_args_init(&new_arg);

			switch (flavor) {
			case CIL_NAME: {
				struct cil_name *name;
				name = __cil_insert_name(args->db, pc->data, current);
				if (name != NULL) {
					new_arg->arg = (struct cil_symtab_datum *)name;
				} else {
					new_arg->arg_str = pc->data;
				}
			}
				break;
			case CIL_TYPE:
				new_arg->arg_str = pc->data;
				break;
			case CIL_ROLE:
				new_arg->arg_str = pc->data;
				break;
			case CIL_USER:
				new_arg->arg_str = pc->data;
				break;
			case CIL_SENS:
				new_arg->arg_str = pc->data;
				break;
			case CIL_CAT:
				new_arg->arg_str = pc->data;
				break;
			case CIL_BOOL:
				new_arg->arg_str = pc->data;
				break;
			case CIL_CATSET: {
				if (pc->cl_head != NULL) {
					struct cil_catset *catset = NULL;
					struct cil_tree_node *cat_node = NULL;
					cil_catset_init(&catset);
					rc = cil_fill_cats(pc, &catset->cats);
					if (rc != SEPOL_OK) {
						cil_destroy_catset(catset);
						goto exit;
					}
					cil_tree_node_init(&cat_node);
					cat_node->flavor = CIL_CATSET;
					cat_node->data = catset;
					cil_list_append(((struct cil_symtab_datum*)catset)->nodes,
									CIL_LIST_ITEM, cat_node);
					new_arg->arg = (struct cil_symtab_datum*)catset;
				} else {
					new_arg->arg_str = pc->data;
				}

				break;
			}
			case CIL_LEVEL: {
				if (pc->cl_head != NULL) {
					struct cil_level *level = NULL;
					struct cil_tree_node *lvl_node = NULL;
					cil_level_init(&level);

					rc = cil_fill_level(pc->cl_head, level);
					if (rc != SEPOL_OK) {
						cil_log(CIL_ERR, "Failed to create anonymous level, rc: %d\n", rc);
						cil_destroy_level(level);
						goto exit;
					}
					cil_tree_node_init(&lvl_node);
					lvl_node->flavor = CIL_LEVEL;
					lvl_node->data = level;
					cil_list_append(((struct cil_symtab_datum*)level)->nodes, 
									CIL_LIST_ITEM, lvl_node);
					new_arg->arg = (struct cil_symtab_datum*)level;
				} else {
					new_arg->arg_str = pc->data;
				}

				break;
			}
			case CIL_LEVELRANGE: {
				if (pc->cl_head != NULL) {
					struct cil_levelrange *range = NULL;
					struct cil_tree_node *range_node = NULL;
					cil_levelrange_init(&range);

					rc = cil_fill_levelrange(pc->cl_head, range);
					if (rc != SEPOL_OK) {
						cil_log(CIL_ERR, "Failed to create anonymous levelrange, rc: %d\n", rc);
						cil_destroy_levelrange(range);
						goto exit;
					}
					cil_tree_node_init(&range_node);
					range_node->flavor = CIL_LEVELRANGE;
					range_node->data = range;
					cil_list_append(((struct cil_symtab_datum*)range)->nodes, 
									CIL_LIST_ITEM, range_node);
					new_arg->arg = (struct cil_symtab_datum*)range;
				} else {
					new_arg->arg_str = pc->data;
				}

				break;
			}
			case CIL_IPADDR: {
				if (pc->cl_head != NULL) {
					struct cil_ipaddr *ipaddr = NULL;
					struct cil_tree_node *addr_node = NULL;
					cil_ipaddr_init(&ipaddr);

					rc = cil_fill_ipaddr(pc->cl_head, ipaddr);
					if (rc != SEPOL_OK) {
						cil_log(CIL_ERR, "Failed to create anonymous ip address, rc; %d\n", rc);
						cil_destroy_ipaddr(ipaddr);
						goto exit;
					}
					cil_tree_node_init(&addr_node);
					addr_node->flavor = CIL_IPADDR;
					addr_node->data = ipaddr;
					cil_list_append(((struct cil_symtab_datum*)ipaddr)->nodes,
									CIL_LIST_ITEM, addr_node);
					new_arg->arg = (struct cil_symtab_datum*)ipaddr;
				} else {
					new_arg->arg_str = pc->data;
				}

				break;
			}
			case CIL_CLASS:
				new_arg->arg_str = pc->data;
				break;
			case CIL_MAP_CLASS:
				new_arg->arg_str = pc->data;
				break;
			case CIL_CLASSPERMISSION: {
				if (pc->cl_head != NULL) {
					struct cil_classpermission *cp = NULL;
					struct cil_tree_node *cp_node = NULL;

					cil_classpermission_init(&cp);
					rc = cil_fill_classperms_list(pc, &cp->classperms);
					if (rc != SEPOL_OK) {
						cil_log(CIL_ERR, "Failed to create anonymous classpermission\n");
						cil_destroy_classpermission(cp);
						goto exit;
					}
					cil_tree_node_init(&cp_node);
					cp_node->flavor = CIL_CLASSPERMISSION;
					cp_node->data = cp;
					cil_list_append(cp->datum.nodes, CIL_LIST_ITEM, cp_node);
					new_arg->arg = (struct cil_symtab_datum*)cp;
				} else {
					new_arg->arg_str = pc->data;
				}
				break;
			}
			default:
				cil_log(CIL_ERR, "Unexpected flavor: %d\n", 
						(((struct cil_param*)item->data)->flavor));
				rc = SEPOL_ERR;
				goto exit;
			}
			new_arg->param_str = ((struct cil_param*)item->data)->str;
			new_arg->flavor = flavor;

			cil_list_append(new_call->args, CIL_ARGS, new_arg);

			pc = pc->next;
		}

		if (pc != NULL) {
			cil_log(CIL_ERR, "Unexpected arguments (%s, line: %d)\n", current->path, current->line);
			rc = SEPOL_ERR;
			goto exit;
		}
	} else if (new_call->args_tree != NULL) {
		cil_log(CIL_ERR, "Unexpected arguments (%s, line: %d)\n", current->path, current->line);
		rc = SEPOL_ERR;
		goto exit;
	}

	if (new_call->copied == 0) {
		new_call->copied = 1;

		rc = cil_check_recursive_call(current, macro_node);
		if (rc != SEPOL_OK) {
			goto exit;
		}

		rc = cil_copy_ast(db, macro_node, current);
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Failed to copy macro, rc: %d\n", rc);
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_call2(struct cil_tree_node *current, void *extra_args)
{
	struct cil_call *new_call = current->data;
	int rc = SEPOL_ERR;
	enum cil_sym_index sym_index = CIL_SYM_UNKNOWN;
	struct cil_list_item *item;

	if (new_call->args == NULL) {
		rc = SEPOL_OK;
		goto exit;
	}

	cil_list_for_each(item, new_call->args) {
		struct cil_args *arg = item->data;
		if (arg->arg == NULL && arg->arg_str == NULL) {
			cil_log(CIL_ERR, "Arguments not created correctly\n");
			rc = SEPOL_ERR;
			goto exit;
		}

		switch (arg->flavor) {
		case CIL_NAME:
			if (arg->arg != NULL) {
				continue; /* No need to resolve */
			} else {
				sym_index = CIL_SYM_NAMES;
			}
			break;
		case CIL_LEVEL:
			if (arg->arg_str == NULL && arg->arg != NULL) {
				continue; // anonymous, no need to resolve
			} else {
				sym_index = CIL_SYM_LEVELS;
			}
			break;
		case CIL_LEVELRANGE:
			if (arg->arg_str == NULL && arg->arg != NULL) {
				continue; // anonymous, no need to resolve
			} else {
				sym_index = CIL_SYM_LEVELRANGES;
			}
			break;
		case CIL_CATSET:
			if (arg->arg_str == NULL && arg->arg != NULL) {
				continue; // anonymous, no need to resolve
			} else {
				sym_index = CIL_SYM_CATS;
			}
			break;
		case CIL_IPADDR:
			if (arg->arg_str == NULL && arg->arg != NULL) {
				continue; // anonymous, no need to resolve
			} else {
				sym_index = CIL_SYM_IPADDRS;
			}
			break;
		case CIL_CLASSPERMISSION:
			if (arg->arg_str == NULL && arg->arg != NULL) {
				continue;
			} else {
				sym_index = CIL_SYM_CLASSPERMSETS;
			}
			break;
		case CIL_TYPE:
			if (arg->arg_str == NULL && arg->arg != NULL) {
				continue; // anonymous, no need to resolve
			} else {
				sym_index = CIL_SYM_TYPES;
			}
			break;
		case CIL_ROLE:
			sym_index = CIL_SYM_ROLES;
			break;
		case CIL_USER:
			sym_index = CIL_SYM_USERS;
			break;
		case CIL_SENS:
			sym_index = CIL_SYM_SENS;
			break;
		case CIL_CAT:
			sym_index = CIL_SYM_CATS;
			break;
		case CIL_CLASS:
		case CIL_MAP_CLASS:
			sym_index = CIL_SYM_CLASSES;
			break;
		case CIL_BOOL:
			sym_index = CIL_SYM_BOOLS;
			break;
		default:
			rc = SEPOL_ERR;
			goto exit;
		}

		if (sym_index != CIL_SYM_UNKNOWN) {
			rc = cil_resolve_name(current, arg->arg_str, sym_index, extra_args, &(arg->arg));
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_name_call_args(struct cil_call *call, char *name, enum cil_sym_index sym_index, struct cil_symtab_datum **datum)
{
	struct cil_list_item *item;
	enum cil_sym_index param_index = CIL_SYM_UNKNOWN;
	int rc = SEPOL_ERR;

	if (call == NULL || name == NULL) {
		goto exit;
	}

	if (call->args == NULL) {
		goto exit;
	}

	cil_list_for_each(item, call->args) {
		struct cil_args * arg = item->data;
		rc = cil_flavor_to_symtab_index(arg->flavor, &param_index);
		if (param_index == sym_index) {
			if (name == arg->param_str) {
				*datum = arg->arg;
				rc = SEPOL_OK;
				goto exit;
			}
		}
	}

	return SEPOL_ERR;

exit:
	return rc;
}

int cil_resolve_expr(enum cil_flavor expr_type, struct cil_list *str_expr, struct cil_list **datum_expr, struct cil_tree_node *parent, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_list_item *curr;
	struct cil_symtab_datum *res_datum = NULL;
	enum cil_sym_index sym_index =  CIL_SYM_UNKNOWN;

	switch (str_expr->flavor) {
	case CIL_BOOL:
		sym_index = CIL_SYM_BOOLS;
		break;
	case CIL_TUNABLE:
		sym_index = CIL_SYM_TUNABLES;
		break;
	case CIL_TYPE:
		sym_index = CIL_SYM_TYPES;
		break;
	case CIL_ROLE:
		sym_index = CIL_SYM_ROLES;
		break;
	case CIL_USER:
		sym_index = CIL_SYM_USERS;
		break;
	case CIL_CAT:
		sym_index = CIL_SYM_CATS;
		break;
	default:
		break;
	}

	cil_list_init(datum_expr, str_expr->flavor);

	cil_list_for_each(curr, str_expr) {
		switch (curr->flavor) {
		case CIL_STRING:
			rc = cil_resolve_name(parent, curr->data, sym_index, extra_args, &res_datum);
			if (rc != SEPOL_OK) {
				goto exit;
			}

			if (sym_index == CIL_SYM_TYPES && (expr_type == CIL_CONSTRAIN || expr_type == CIL_VALIDATETRANS)) {
				cil_type_used(res_datum);
			}

			cil_list_append(*datum_expr, CIL_DATUM, res_datum);
			break;
		case CIL_LIST: {
			struct cil_list *datum_sub_expr;
			rc = cil_resolve_expr(expr_type, curr->data, &datum_sub_expr, parent, extra_args);
			if (rc != SEPOL_OK) {
				cil_list_destroy(&datum_sub_expr, CIL_TRUE);
				goto exit;
			}
			cil_list_append(*datum_expr, CIL_LIST, datum_sub_expr);
			break;
		}
		default:
			cil_list_append(*datum_expr, curr->flavor, curr->data);
			break;
		}				
	}
	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_boolif(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_booleanif *bif = (struct cil_booleanif*)current->data;

	rc = cil_resolve_expr(CIL_BOOLEANIF, bif->str_expr, &bif->datum_expr, current, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	return rc;
}

static int __cil_evaluate_tunable_expr(struct cil_list_item *curr);

static int __cil_evaluate_tunable_expr_helper(struct cil_list_item *curr)
{
	if (curr == NULL) {
		return CIL_FALSE;
	} else if (curr->flavor == CIL_DATUM) {
		struct cil_tunable *tun = curr->data;
		return tun->value;
	} else if (curr->flavor == CIL_LIST) {
		struct cil_list *l = curr->data;
		return __cil_evaluate_tunable_expr(l->head);
	} else {
		return CIL_FALSE;
	}
}

static int __cil_evaluate_tunable_expr(struct cil_list_item *curr)
{
	/* Assumes expression is well-formed */

	if (curr == NULL) {
		return CIL_FALSE;
	} else if (curr->flavor == CIL_OP) {
		uint16_t v1, v2;
		enum cil_flavor op_flavor = (enum cil_flavor)curr->data;

		v1 = __cil_evaluate_tunable_expr_helper(curr->next);

		if (op_flavor == CIL_NOT) return !v1;

		v2 = __cil_evaluate_tunable_expr_helper(curr->next->next);

		if (op_flavor == CIL_AND) return (v1 && v2);
		else if (op_flavor == CIL_OR) return (v1 || v2);
		else if (op_flavor == CIL_XOR) return (v1 ^ v2);
		else if (op_flavor == CIL_EQ) return (v1 == v2);
		else if (op_flavor == CIL_NEQ) return (v1 != v2);
		else return CIL_FALSE;
	} else {
		uint16_t v;
		for (;curr; curr = curr->next) {
			v = __cil_evaluate_tunable_expr_helper(curr);
			if (v) return v;
		}
		return CIL_FALSE;
	}
}

int cil_resolve_tunif(struct cil_tree_node *current, void *extra_args)
{
	struct cil_args_resolve *args = extra_args;
	struct cil_db *db = NULL;
	int rc = SEPOL_ERR;
	struct cil_tunableif *tif = (struct cil_tunableif*)current->data;
	uint16_t result = CIL_FALSE;
	struct cil_tree_node *true_node = NULL;
	struct cil_tree_node *false_node = NULL;
	struct cil_condblock *cb = NULL;

	if (args != NULL) {
		db = args->db;
	}

	rc = cil_resolve_expr(CIL_TUNABLEIF, tif->str_expr, &tif->datum_expr, current, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	result = __cil_evaluate_tunable_expr(tif->datum_expr->head);

	if (current->cl_head != NULL && current->cl_head->flavor == CIL_CONDBLOCK) {
		cb = current->cl_head->data;
		if (cb->flavor == CIL_CONDTRUE) {
			true_node = current->cl_head;
		} else if (cb->flavor == CIL_CONDFALSE) {
			false_node = current->cl_head;
		}
	}

	if (current->cl_head != NULL && current->cl_head->next != NULL && current->cl_head->next->flavor == CIL_CONDBLOCK) {
		cb = current->cl_head->next->data;
		if (cb->flavor == CIL_CONDTRUE) {
			true_node = current->cl_head->next;
		} else if (cb->flavor == CIL_CONDFALSE) {
			false_node = current->cl_head->next;
		}
	}

	if (result == CIL_TRUE) {
		if (true_node != NULL) {
			rc = cil_copy_ast(db, true_node, current->parent);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	} else {
		if (false_node != NULL) {
			rc = cil_copy_ast(db, false_node, current->parent);
			if (rc  != SEPOL_OK) {
				goto exit;
			}
		}
	}

	cil_tree_children_destroy(current);
	current->cl_head = NULL;
	current->cl_tail = NULL;

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_userattributeset(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_userattributeset *attrusers = current->data;
	struct cil_symtab_datum *attr_datum = NULL;
	struct cil_tree_node *attr_node = NULL;
	struct cil_userattribute *attr = NULL;

	rc = cil_resolve_name(current, attrusers->attr_str, CIL_SYM_USERS, extra_args, &attr_datum);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	attr_node = attr_datum->nodes->head->data;

	if (attr_node->flavor != CIL_USERATTRIBUTE) {
		rc = SEPOL_ERR;
		cil_log(CIL_ERR, "Attribute user not an attribute\n");
		goto exit;
	}
	attr = (struct cil_userattribute*)attr_datum;

	rc = cil_resolve_expr(CIL_USERATTRIBUTESET, attrusers->str_expr, &attrusers->datum_expr, current, extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_verify_no_self_reference(attr_datum, attrusers->datum_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (attr->expr_list == NULL) {
		cil_list_init(&attr->expr_list, CIL_USERATTRIBUTE);
	}

	cil_list_append(attr->expr_list, CIL_LIST, attrusers->datum_expr);

	return SEPOL_OK;

exit:
	return rc;
}

int __cil_resolve_ast_node(struct cil_tree_node *node, void *extra_args)
{
	int rc = SEPOL_OK;
	struct cil_args_resolve *args = extra_args;
	enum cil_pass pass = 0;
	struct cil_list *ins = args->in_list;

	if (node == NULL || args == NULL) {
		goto exit;
	}

	pass = args->pass;
	switch (pass) {
	case CIL_PASS_TIF:
		if (node->flavor == CIL_TUNABLEIF) {
			rc = cil_resolve_tunif(node, args);
		}
		break;
	case CIL_PASS_IN:
		if (node->flavor == CIL_IN) {
			// due to ordering issues, in statements are just gathered here and
			// resolved together in cil_resolve_in_list once all are found
			cil_list_prepend(ins, CIL_NODE, node);
		}
		break;
	case CIL_PASS_BLKIN_LINK:
		if (node->flavor == CIL_BLOCKINHERIT) {
			rc = cil_resolve_blockinherit_link(node, args);
		}
		break;
	case CIL_PASS_BLKIN_COPY:
		if (node->flavor == CIL_BLOCK) {
			rc = cil_resolve_blockinherit_copy(node, args);
		}
		break;
	case CIL_PASS_BLKABS:
		if (node->flavor == CIL_BLOCKABSTRACT) {
			rc = cil_resolve_blockabstract(node, args);
		}
		break;
	case CIL_PASS_MACRO:
		if (node->flavor == CIL_CALL && args->macro != NULL) {
			rc = cil_resolve_call1(node, args);
		}
		break;
	case CIL_PASS_CALL1:
		if (node->flavor == CIL_CALL) {
			rc = cil_resolve_call1(node, args);
		}
		break;
	case CIL_PASS_CALL2:
		if (node->flavor == CIL_CALL) {
			rc = cil_resolve_call2(node, args);
		}
		break;
	case CIL_PASS_ALIAS1:
		switch (node->flavor) {
		case CIL_TYPEALIASACTUAL:
			rc = cil_resolve_aliasactual(node, args, CIL_TYPE);
			break;
		case CIL_SENSALIASACTUAL:
			rc = cil_resolve_aliasactual(node, args, CIL_SENS);
			break;
		case CIL_CATALIASACTUAL:
			rc = cil_resolve_aliasactual(node, args, CIL_CAT);
			break;
		default: 
			break;
		}
		break;
	case CIL_PASS_ALIAS2:
		switch (node->flavor) {
		case CIL_TYPEALIAS:
			rc = cil_resolve_alias_to_actual(node, CIL_TYPE);
			break;
		case CIL_SENSALIAS:
			rc = cil_resolve_alias_to_actual(node, CIL_SENS);
			break;
		case CIL_CATALIAS:
			rc = cil_resolve_alias_to_actual(node, CIL_CAT);
			break;
		default:
			break;
		}
		break;
	case CIL_PASS_MISC1:
		switch (node->flavor) {
		case CIL_SIDORDER:
			rc = cil_resolve_sidorder(node, args);
			break;
		case CIL_CLASSORDER:
			rc = cil_resolve_classorder(node, args);
			break;
		case CIL_CATORDER:
			rc = cil_resolve_catorder(node, args);
			break;
		case CIL_SENSITIVITYORDER:
			rc = cil_resolve_sensitivityorder(node, args);
			break;
		case CIL_BOOLEANIF:
			rc = cil_resolve_boolif(node, args);
			break;
		default:
			break;
		}
		break;
	case CIL_PASS_MLS:
		switch (node->flavor) {
		case CIL_CATSET:
			rc = cil_resolve_catset(node, (struct cil_catset*)node->data, args);
			break;
		default:
			break;
		}
		break;
	case CIL_PASS_MISC2:
		switch (node->flavor) {
		case CIL_SENSCAT:
			rc = cil_resolve_senscat(node, args);
			break;
		case CIL_CLASSCOMMON:
			rc = cil_resolve_classcommon(node, args);
			break;
		default:
			break;
		}
		break;
	case CIL_PASS_MISC3:
		switch (node->flavor) {
		case CIL_TYPEATTRIBUTESET:
			rc = cil_resolve_typeattributeset(node, args);
			break;
		case CIL_TYPEBOUNDS:
			rc = cil_resolve_bounds(node, args, CIL_TYPE);
			break;
		case CIL_TYPEPERMISSIVE:
			rc = cil_resolve_typepermissive(node, args);
			break;
		case CIL_NAMETYPETRANSITION:
			rc = cil_resolve_nametypetransition(node, args);
			break;
		case CIL_RANGETRANSITION:
			rc = cil_resolve_rangetransition(node, args);
			break;
		case CIL_CLASSPERMISSIONSET:
			rc = cil_resolve_classpermissionset(node, (struct cil_classpermissionset*)node->data, args);
			break;
		case CIL_CLASSMAPPING:
			rc = cil_resolve_classmapping(node, args);
			break;
		case CIL_AVRULE:
		case CIL_AVRULEX:
			rc = cil_resolve_avrule(node, args);
			break;
		case CIL_PERMISSIONX:
			rc = cil_resolve_permissionx(node, (struct cil_permissionx*)node->data, args);
			break;
		case CIL_TYPE_RULE:
			rc = cil_resolve_type_rule(node, args);
			break;
		case CIL_USERROLE:
			rc = cil_resolve_userrole(node, args);
			break;
		case CIL_USERLEVEL:
			rc = cil_resolve_userlevel(node, args);
			break;
		case CIL_USERRANGE:
			rc = cil_resolve_userrange(node, args);
			break;
		case CIL_USERBOUNDS:
			rc = cil_resolve_bounds(node, args, CIL_USER);
			break;
		case CIL_USERPREFIX:
			rc = cil_resolve_userprefix(node, args);
			break;
		case CIL_SELINUXUSER:
		case CIL_SELINUXUSERDEFAULT:
			rc = cil_resolve_selinuxuser(node, args);
			break;
		case CIL_ROLEATTRIBUTESET:
			rc = cil_resolve_roleattributeset(node, args);
			break;
		case CIL_ROLETYPE:
			rc = cil_resolve_roletype(node, args);
			break;
		case CIL_ROLETRANSITION:
			rc = cil_resolve_roletransition(node, args);
			break;
		case CIL_ROLEALLOW:
			rc = cil_resolve_roleallow(node, args);
			break;
		case CIL_ROLEBOUNDS:
			rc = cil_resolve_bounds(node, args, CIL_ROLE);
			break;
		case CIL_LEVEL:
			rc = cil_resolve_level(node, (struct cil_level*)node->data, args);
			break;
		case CIL_LEVELRANGE:
			rc = cil_resolve_levelrange(node, (struct cil_levelrange*)node->data, args);
			break;
		case CIL_CONSTRAIN:
			rc = cil_resolve_constrain(node, args);
			break;
		case CIL_MLSCONSTRAIN:
			rc = cil_resolve_constrain(node, args);
			break;
		case CIL_VALIDATETRANS:
		case CIL_MLSVALIDATETRANS:
			rc = cil_resolve_validatetrans(node, args);
			break;
		case CIL_CONTEXT:
			rc = cil_resolve_context(node, (struct cil_context*)node->data, args);
			break;
		case CIL_FILECON:
			rc = cil_resolve_filecon(node, args);
			break;
		case CIL_PORTCON:
			rc = cil_resolve_portcon(node, args);
			break;
		case CIL_NODECON:
			rc = cil_resolve_nodecon(node, args);
			break;
		case CIL_GENFSCON:
			rc = cil_resolve_genfscon(node, args);
			break;
		case CIL_NETIFCON:
			rc = cil_resolve_netifcon(node, args);
			break;
		case CIL_PIRQCON:
			rc = cil_resolve_pirqcon(node, args);
			break;
		case CIL_IOMEMCON:
			rc = cil_resolve_iomemcon(node, args);
			break;
		case CIL_IOPORTCON:
			rc = cil_resolve_ioportcon(node, args);
			break;
		case CIL_PCIDEVICECON:
			rc = cil_resolve_pcidevicecon(node, args);
			break;
		case CIL_DEVICETREECON:
			rc = cil_resolve_devicetreecon(node, args);
			break;
		case CIL_FSUSE:
			rc = cil_resolve_fsuse(node, args);
			break;
		case CIL_SIDCONTEXT:
			rc = cil_resolve_sidcontext(node, args);
			break;
		case CIL_DEFAULTUSER:
		case CIL_DEFAULTROLE:
		case CIL_DEFAULTTYPE:
			rc = cil_resolve_default(node, args);
			break;
		case CIL_DEFAULTRANGE:
			rc = cil_resolve_defaultrange(node, args);
			break;
		case CIL_USERATTRIBUTESET:
			rc = cil_resolve_userattributeset(node, args);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return rc;

exit:
	return rc;
}

int __cil_resolve_ast_node_helper(struct cil_tree_node *node, __attribute__((unused)) uint32_t *finished, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_args_resolve *args = extra_args;
	enum cil_pass pass = args->pass;
	struct cil_tree_node *optstack = args->optstack;
	struct cil_tree_node *boolif = args->boolif;
	struct cil_tree_node *blockstack = args->blockstack;
	struct cil_tree_node *macro = args->macro;

	if (node == NULL) {
		goto exit;
	}

	if (optstack != NULL) {
		if (node->flavor == CIL_TUNABLE || node->flavor == CIL_MACRO) {
			/* tuanbles and macros are not allowed in optionals*/
			cil_log(CIL_ERR, "%s statement is not allowed in optionals (%s:%d)\n", cil_node_to_string(node), node->path, node->line);
			rc = SEPOL_ERR;
			goto exit;
		}
	}

	if (blockstack != NULL) {
		if (node->flavor == CIL_CAT || node->flavor == CIL_SENS) {
			cil_log(CIL_ERR, "%s statement is not allowed in blocks (%s:%d)\n", cil_node_to_string(node), node->path, node->line);
			rc = SEPOL_ERR;
			goto exit;
		}
	}

	if (macro != NULL) {
		if (node->flavor == CIL_BLOCKINHERIT ||
			node->flavor == CIL_BLOCK ||
			node->flavor == CIL_BLOCKABSTRACT ||
			node->flavor == CIL_MACRO) {
			cil_log(CIL_ERR, "%s statement is not allowed in macros (%s:%d)\n", cil_node_to_string(node), node->path, node->line);
			rc = SEPOL_ERR;
			goto exit;
		}
	}

	if (boolif != NULL) {
		if (!(node->flavor == CIL_CONDBLOCK ||
			node->flavor == CIL_AVRULE ||
			node->flavor == CIL_TYPE_RULE ||
			node->flavor == CIL_CALL ||
			node->flavor == CIL_TUNABLEIF ||
			node->flavor == CIL_NAMETYPETRANSITION)) {
			if (((struct cil_booleanif*)boolif->data)->preserved_tunable) {
				cil_log(CIL_ERR, "%s statement is not allowed in booleanifs (tunableif treated as a booleanif) (%s:%d)\n", cil_node_to_string(node), node->path, node->line);
			} else {
				cil_log(CIL_ERR, "%s statement is not allowed in booleanifs (%s:%d)\n", cil_node_to_string(node), node->path, node->line);
			}
			rc = SEPOL_ERR;
			goto exit;
		}
	}

	if (node->flavor == CIL_MACRO) {
		if (pass != CIL_PASS_TIF && pass != CIL_PASS_MACRO) {
			*finished = CIL_TREE_SKIP_HEAD;
			rc = SEPOL_OK;
			goto exit;
		}
	}

	if (node->flavor == CIL_BLOCK && ((((struct cil_block*)node->data)->is_abstract == CIL_TRUE) && (pass > CIL_PASS_BLKABS))) {
		*finished = CIL_TREE_SKIP_HEAD;
		rc = SEPOL_OK;
		goto exit;
	}

	rc = __cil_resolve_ast_node(node, extra_args);
	if (rc == SEPOL_ENOENT) {
		enum cil_log_level lvl = CIL_ERR;

		if (optstack != NULL) {
			lvl = CIL_WARN;

			struct cil_optional *opt = (struct cil_optional *)optstack->data;
			struct cil_tree_node *opt_node = opt->datum.nodes->head->data;
			cil_log(lvl, "Disabling optional '%s' at line %d of %s: ", opt->datum.name, opt_node->line, opt_node->path);
			/* disable an optional if something failed to resolve */
			opt->enabled = CIL_FALSE;
			rc = SEPOL_OK;
		}

		cil_log(lvl, "Failed to resolve '%s' in %s statement at line %d of %s\n",
		        args->last_resolved_name, cil_node_to_string(node), node->line, node->path);
		goto exit;
	}

	return rc;

exit:
	return rc;
}

int __cil_resolve_ast_first_child_helper(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_args_resolve *args = extra_args;
	struct cil_tree_node *optstack = NULL;
	struct cil_tree_node *parent = NULL;
	struct cil_tree_node *blockstack = NULL;
	struct cil_tree_node *new = NULL;

	if (current == NULL || extra_args == NULL) {
		goto exit;
	}

	optstack = args->optstack;
	parent = current->parent;
	blockstack = args->blockstack;

	if (parent->flavor == CIL_OPTIONAL || parent->flavor == CIL_BLOCK) {
		/* push this node onto a stack */
		cil_tree_node_init(&new);

		new->data = parent->data;
		new->flavor = parent->flavor;

		if (parent->flavor == CIL_OPTIONAL) {
			if (optstack != NULL) {
				optstack->parent = new;
				new->cl_head = optstack;
			}
			args->optstack = new;
		} else if (parent->flavor == CIL_BLOCK) {
			if (blockstack != NULL) {
				blockstack->parent = new;
				new->cl_head = blockstack;
			}
			args->blockstack = new;
		}
	} else if (parent->flavor == CIL_BOOLEANIF) {
		args->boolif = parent;
	} else if (parent->flavor == CIL_MACRO) {
		args->macro = parent;
	}

	return SEPOL_OK;

exit:
	return rc;

}

int __cil_resolve_ast_last_child_helper(struct cil_tree_node *current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_args_resolve *args = extra_args;
	struct cil_tree_node *parent = NULL;
	struct cil_tree_node *blockstack = NULL;

	if (current == NULL ||  extra_args == NULL) {
		goto exit;
	}

	parent = current->parent;

	if (parent->flavor == CIL_MACRO) {
		args->macro = NULL;
	} else if (parent->flavor == CIL_OPTIONAL) {
		struct cil_tree_node *optstack;

		if (((struct cil_optional *)parent->data)->enabled == CIL_FALSE) {
			*(args->changed) = CIL_TRUE;
			cil_tree_children_destroy(parent);
		}

		/* pop off the stack */
		optstack = args->optstack;
		args->optstack = optstack->cl_head;
		if (optstack->cl_head) {
			optstack->cl_head->parent = NULL;
		}
		free(optstack);
	} else if (parent->flavor == CIL_BOOLEANIF) {
		args->boolif = NULL;
	} else if (parent->flavor == CIL_BLOCK) {
		/* pop off the stack */
		blockstack = args->blockstack;
		args->blockstack = blockstack->cl_head;
		if (blockstack->cl_head) {
			blockstack->cl_head->parent = NULL;
		}
		free(blockstack);
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_resolve_ast(struct cil_db *db, struct cil_tree_node *current)
{
	int rc = SEPOL_ERR;
	struct cil_args_resolve extra_args;
	enum cil_pass pass = CIL_PASS_TIF;
	uint32_t changed = 0;

	if (db == NULL || current == NULL) {
		goto exit;
	}

	extra_args.db = db;
	extra_args.pass = pass;
	extra_args.changed = &changed;
	extra_args.last_resolved_name = NULL;
	extra_args.optstack = NULL;
	extra_args.boolif= NULL;
	extra_args.macro = NULL;
	extra_args.sidorder_lists = NULL;
	extra_args.classorder_lists = NULL;
	extra_args.unordered_classorder_lists = NULL;
	extra_args.catorder_lists = NULL;
	extra_args.sensitivityorder_lists = NULL;
	extra_args.in_list = NULL;
	extra_args.blockstack = NULL;

	cil_list_init(&extra_args.sidorder_lists, CIL_LIST_ITEM);
	cil_list_init(&extra_args.classorder_lists, CIL_LIST_ITEM);
	cil_list_init(&extra_args.unordered_classorder_lists, CIL_LIST_ITEM);
	cil_list_init(&extra_args.catorder_lists, CIL_LIST_ITEM);
	cil_list_init(&extra_args.sensitivityorder_lists, CIL_LIST_ITEM);
	cil_list_init(&extra_args.in_list, CIL_IN);
	for (pass = CIL_PASS_TIF; pass < CIL_PASS_NUM; pass++) {
		extra_args.pass = pass;
		rc = cil_tree_walk(current, __cil_resolve_ast_node_helper, __cil_resolve_ast_first_child_helper, __cil_resolve_ast_last_child_helper, &extra_args);
		if (rc != SEPOL_OK) {
			cil_log(CIL_INFO, "Pass %i of resolution failed\n", pass);
			goto exit;
		}

		if (pass == CIL_PASS_IN) {
			rc = cil_resolve_in_list(&extra_args);
			if (rc != SEPOL_OK) {
				goto exit;
			}
			cil_list_destroy(&extra_args.in_list, CIL_FALSE);
		}

		if (pass == CIL_PASS_MISC1) {
			db->sidorder = __cil_ordered_lists_merge_all(&extra_args.sidorder_lists, NULL);
			if (db->sidorder == NULL) {
				rc = SEPOL_ERR;
				goto exit;
			}
			db->classorder = __cil_ordered_lists_merge_all(&extra_args.classorder_lists, &extra_args.unordered_classorder_lists);
			if (db->classorder == NULL) {
				rc = SEPOL_ERR;
				goto exit;
			}
			db->catorder = __cil_ordered_lists_merge_all(&extra_args.catorder_lists, NULL);
			if (db->catorder == NULL) {
				rc = SEPOL_ERR;
				goto exit;
			}
			cil_set_cat_values(db->catorder, db);
			db->sensitivityorder = __cil_ordered_lists_merge_all(&extra_args.sensitivityorder_lists, NULL);
			if (db->sensitivityorder == NULL) {
				rc = SEPOL_ERR;
				goto exit;
			}

			rc = __cil_verify_ordered(current, CIL_SID);
			if (rc != SEPOL_OK) {
				goto exit;
			}

			rc = __cil_verify_ordered(current, CIL_CLASS);
			if (rc != SEPOL_OK) {
				goto exit;
			}

			rc = __cil_verify_ordered(current, CIL_CAT);
			if (rc != SEPOL_OK) {
				goto exit;
			}

			rc = __cil_verify_ordered(current, CIL_SENS);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}

		if (changed && (pass > CIL_PASS_CALL1)) {
			/* Need to re-resolve because an optional was disabled that contained
			 * one or more declarations. We only need to reset to the call1 pass 
			 * because things done in the preceeding passes aren't allowed in 
			 * optionals, and thus can't be disabled.
			 * Note: set pass to CIL_PASS_CALL1 because the pass++ will increment 
			 * it to CIL_PASS_CALL2
			 */
			cil_log(CIL_INFO, "Resetting declarations\n");

			if (pass >= CIL_PASS_MISC1) {
				__cil_ordered_lists_reset(&extra_args.sidorder_lists);
				__cil_ordered_lists_reset(&extra_args.classorder_lists);
				__cil_ordered_lists_reset(&extra_args.unordered_classorder_lists);
				__cil_ordered_lists_reset(&extra_args.catorder_lists);
				__cil_ordered_lists_reset(&extra_args.sensitivityorder_lists);
				cil_list_destroy(&db->sidorder, CIL_FALSE);
				cil_list_destroy(&db->classorder, CIL_FALSE);
				cil_list_destroy(&db->catorder, CIL_FALSE);
				cil_list_destroy(&db->sensitivityorder, CIL_FALSE);
			}

			pass = CIL_PASS_CALL1;

			rc = cil_reset_ast(current);
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Failed to reset declarations\n");
				goto exit;
			}
		}

		/* reset the arguments */
		changed = 0;
		while (extra_args.optstack != NULL) {
			struct cil_tree_node *curr = extra_args.optstack;
			struct cil_tree_node *next = curr->cl_head;
			free(curr);
			extra_args.optstack = next;
		}
		while (extra_args.blockstack!= NULL) {
			struct cil_tree_node *curr = extra_args.blockstack;
			struct cil_tree_node *next = curr->cl_head;
			free(curr);
			extra_args.blockstack= next;
		}
	}

	rc = __cil_verify_initsids(db->sidorder);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = SEPOL_OK;
exit:
	__cil_ordered_lists_destroy(&extra_args.sidorder_lists);
	__cil_ordered_lists_destroy(&extra_args.classorder_lists);
	__cil_ordered_lists_destroy(&extra_args.catorder_lists);
	__cil_ordered_lists_destroy(&extra_args.sensitivityorder_lists);
	cil_list_destroy(&extra_args.in_list, CIL_FALSE);

	return rc;
}

static int __cil_resolve_name_with_root(struct cil_db *db, char *name, enum cil_sym_index sym_index, struct cil_symtab_datum **datum)
{
	symtab_t *symtab = &((struct cil_root *)db->ast->root->data)->symtab[sym_index];

	return cil_symtab_get_datum(symtab, name, datum);
}

static int __cil_resolve_name_with_parents(struct cil_tree_node *node, char *name, enum cil_sym_index sym_index, struct cil_symtab_datum **datum)
{
	int rc = SEPOL_ERR;
	symtab_t *symtab = NULL;

	while (node != NULL && rc != SEPOL_OK) {
		switch (node->flavor) {
		case CIL_ROOT:
			goto exit;
			break;
		case CIL_BLOCK:
			symtab = &((struct cil_block*)node->data)->symtab[sym_index];
			rc = cil_symtab_get_datum(symtab, name, datum);
			break;
		case CIL_BLOCKINHERIT: {
			struct cil_blockinherit *inherit = node->data;
			rc = __cil_resolve_name_with_parents(node->parent, name, sym_index, datum);
			if (rc != SEPOL_OK) {
				/* Continue search in original block's parent */
				rc = __cil_resolve_name_with_parents(NODE(inherit->block), name, sym_index, datum);
				goto exit;
			}
		}
			break;
		case CIL_MACRO: {
			struct cil_macro *macro = node->data;
			symtab = &macro->symtab[sym_index];
			rc = cil_symtab_get_datum(symtab, name, datum);
		}
			break;
		case CIL_CALL: {
			struct cil_call *call = node->data;
			rc = cil_resolve_name_call_args(call, name, sym_index, datum);
			if (rc != SEPOL_OK) {
				/* Continue search in macro's parent */
				rc = __cil_resolve_name_with_parents(NODE(call->macro)->parent, name, sym_index, datum);
			}
		}
			break;
		case CIL_IN:
			/* In block symtabs only exist before resolving the AST */
		case CIL_CONDBLOCK:
			/* Cond block symtabs only exist before resolving the AST */
		default:
			break;
		}

		node = node->parent;
	}

exit:
	return rc;
}

static int __cil_resolve_name_helper(struct cil_db *db, struct cil_tree_node *node, char *name, enum cil_sym_index sym_index, struct cil_symtab_datum **datum)
{
	int rc = SEPOL_ERR;

	rc = __cil_resolve_name_with_parents(node, name, sym_index, datum);
	if (rc != SEPOL_OK) {
		rc = __cil_resolve_name_with_root(db, name, sym_index, datum);
	}
	return rc;
}

int cil_resolve_name(struct cil_tree_node *ast_node, char *name, enum cil_sym_index sym_index, void *extra_args, struct cil_symtab_datum **datum)
{
	int rc = SEPOL_ERR;
	struct cil_args_resolve *args = extra_args;
	struct cil_db *db = args->db;
	struct cil_tree_node *node = NULL;

	if (name == NULL) {
		cil_log(CIL_ERR, "Invalid call to cil_resolve_name\n");
		goto exit;
	}

	*datum = NULL;

	if (strchr(name,'.') == NULL) {
		/* No '.' in name */
		rc = __cil_resolve_name_helper(db, ast_node->parent, name, sym_index, datum);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	} else {
		char *sp = NULL;
		char *name_dup = cil_strdup(name);
		char *current = strtok_r(name_dup, ".", &sp);
		char *next = strtok_r(NULL, ".", &sp);
		symtab_t *symtab = NULL;
		
		node = ast_node;
		if (*name == '.') {
			/* Leading '.' */
			symtab = &((struct cil_root *)db->ast->root->data)->symtab[CIL_SYM_BLOCKS];
		} else {
			rc = __cil_resolve_name_helper(db, node->parent, current, CIL_SYM_BLOCKS, datum);
			if (rc != SEPOL_OK) {
				free(name_dup);
				goto exit;
			}
			symtab = (*datum)->symtab;
		}
		/* Keep looking up blocks by name until only last part of name remains */
		while (next != NULL) {
			rc = cil_symtab_get_datum(symtab, current, datum);
			if (rc != SEPOL_OK) {
				free(name_dup);
				goto exit;
			}
			node = NODE(*datum);
			if (node->flavor == CIL_BLOCK) {
				symtab = &((struct cil_block*)node->data)->symtab[CIL_SYM_BLOCKS];
			} else {
				if (ast_node->flavor != CIL_IN) {
					cil_log(CIL_WARN, "Can only use %s name for name resolution in \"in\" blocks\n", cil_node_to_string(node));
					free(name_dup);
					rc = SEPOL_ERR;
					goto exit;
				}
				if (node->flavor == CIL_MACRO) {
					struct cil_macro *macro = node->data;
					symtab = &macro->symtab[sym_index];
				} else {
					/* optional */
					symtab = (*datum)->symtab;
				}
			}
			current = next;
			next = strtok_r(NULL, ".", &sp);
		}
		symtab = &(symtab[sym_index]);
		rc = cil_symtab_get_datum(symtab, current, datum);
		free(name_dup);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	rc = SEPOL_OK;

exit:
	if (rc != SEPOL_OK) {
		*datum = NULL;
	}

	if (*datum != NULL) {
		/* If this datum is an alias, then return the actual node
		 * This depends on aliases already being processed
		 */
		node = NODE(*datum);
		if (node->flavor == CIL_TYPEALIAS || node->flavor == CIL_SENSALIAS
			|| node->flavor == CIL_CATALIAS) {
			struct cil_alias *alias = (struct cil_alias *)(*datum);
			if (alias->actual) {
				*datum = alias->actual;
			}
		}
	}

	args->last_resolved_name = name;

	return rc;
}
